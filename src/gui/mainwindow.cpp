#include "mainwindow.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "screen.hpp"

using namespace k86;

namespace {

// Fake device for --dry-run: answers with the echoed command byte and an acknowledge (1).
class EchoTransport : public Transport {
public:
    void sendFeature(const Report& r) override { last_ = r[0]; }
    Report getFeature() override {
        Report r{};
        r[0] = last_;
        r[1] = 1;
        return r;
    }

private:
    uint8_t last_ = 0;
};

QString hexColor(Rgb c) { return QString("#%1").arg(c & 0xFFFFFF, 6, 16, QChar('0')); }

void setButtonColor(QPushButton* b, Rgb c) {
    b->setText(hexColor(c));
    b->setStyleSheet(QString("background-color: %1; color: %2;")
                         .arg(hexColor(c))
                         .arg(((c >> 16 & 0xFF) * 3 + (c >> 8 & 0xFF) * 6 + (c & 0xFF)) / 10 > 128 ? "black" : "white"));
}

QPushButton* button(const QString& text, QWidget* parent) { return new QPushButton(text, parent); }

}  // namespace

MainWindow::MainWindow(const QString& dryRunModel, QWidget* parent) : QMainWindow(parent), dryRun_(dryRunModel) {
    setWindowTitle("Attack Shark K86");
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    auto* top = new QHBoxLayout;
    deviceCombo_ = new QComboBox(central);
    deviceCombo_->setMinimumWidth(320);
    auto* refresh = button("Refresh", central);
    auto* connectBtn = button("Connect", central);
    top->addWidget(new QLabel("Keyboard:", central));
    top->addWidget(deviceCombo_, 1);
    top->addWidget(refresh);
    top->addWidget(connectBtn);
    root->addLayout(top);

    status_ = new QLabel("Not connected", central);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(status_);

    tabs_ = new QTabWidget(central);
    tabs_->addTab(buildLightTab(), "Lighting");
    tabs_->addTab(buildKeysTab(), "Key mapping");
    tabs_->addTab(buildPerKeyTab(), "Per-key colours");
    tabs_->addTab(buildMacroTab(), "Macros");
    tabs_->addTab(buildScreenTab(), "Screen");
    tabs_->addTab(buildSettingsTab(), "Settings");
    tabs_->setEnabled(false);
    root->addWidget(tabs_, 1);
    setCentralWidget(central);
    resize(900, 680);

    connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshDevices);
    connect(connectBtn, &QPushButton::clicked, this, &MainWindow::connectDevice);
    connect(&sysTimer_, &QTimer::timeout, this, &MainWindow::sendSysInfo);

    refreshDevices();
    if (deviceCombo_->count() == 1) connectDevice();
}

MainWindow::~MainWindow() = default;

template <typename F>
void MainWindow::guarded(F&& f) {
    if (!requireKeyboard()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        f();
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        QMessageBox::warning(this, "Error", e.what());
        return;
    }
    QApplication::restoreOverrideCursor();
}

bool MainWindow::requireKeyboard() {
    if (kb_) return true;
    QMessageBox::information(this, "Not connected", "Connect a keyboard first.");
    return false;
}

void MainWindow::refreshDevices() {
    deviceCombo_->clear();
    if (!dryRun_.isEmpty()) {
        deviceCombo_->addItem(QString("Dry run (%1, no hardware)").arg(dryRun_), QString());
        return;
    }
    for (const auto& c : findCandidates())
        deviceCombo_->addItem(QString("%1  %2  %3")
                                  .arg(QString::fromStdString(c.info.path), QString::fromStdString(c.model->displayName),
                                       QString::fromStdString(c.info.name)),
                              QString::fromStdString(c.info.path));
    if (deviceCombo_->count() == 0) status_->setText("No K86 found. Connect it with the USB cable and check permissions (udev rule).");
}

void MainWindow::connectDevice() {
    sysTimer_.stop();
    if (sysSync_) sysSync_->setChecked(false);
    kb_.reset();
    transport_.reset();
    tabs_->setEnabled(false);
    try {
        if (!dryRun_.isEmpty()) {
            transport_ = std::make_unique<EchoTransport>();
            const DeviceModel& m = dryRun_ == "v1" ? knownModels()[0] : knownModels()[1];
            kb_ = makeKeyboard(*transport_, m, [](int) {});
            status_->setText(QString::fromStdString(m.displayName) + " - dry run");
        } else {
            if (deviceCombo_->count() == 0) throw std::runtime_error("no keyboard selected");
            OpenKeyboard ok = openKeyboard(deviceCombo_->currentData().toString().toStdString(), false);
            Versions v = ok.keyboard->readVersions();
            QString text = QString("%1 (%2) at %3, firmware id %4")
                               .arg(QString::fromStdString(ok.keyboard->model().displayName),
                                    QString::fromStdString(ok.keyboard->model().firmwareName),
                                    QString::fromStdString(ok.info.path))
                               .arg(ok.firmwareId ? QString::number(*ok.firmwareId) : "?");
            if (v.usb) text += QString(", USB fw 0x%1").arg(*v.usb, 4, 16, QChar('0'));
            if (v.oled) text += QString(", screen fw 0x%1").arg(*v.oled, 4, 16, QChar('0'));
            status_->setText(text);
            transport_ = std::move(ok.transport);
            kb_ = std::move(ok.keyboard);
        }
    } catch (const std::exception& e) {
        status_->setText(QString("Connection failed: ") + e.what());
        return;
    }
    populateForModel();
    tabs_->setEnabled(true);
    readLight(false);
    if (!kb_->model().sideLightModes.empty()) readLight(true);
    readSettings();
}

// ----------------------------------------------------------------------------- lighting
QWidget* MainWindow::buildLightGroup(const QString& title, LightControls& c, bool side) {
    auto* box = new QGroupBox(title);
    auto* form = new QFormLayout(box);
    c.mode = new QComboBox(box);
    c.brightness = new QSlider(Qt::Horizontal, box);
    c.brightness->setRange(0, 4);
    c.speed = new QSlider(Qt::Horizontal, box);
    c.option = new QSpinBox(box);
    c.color = new QPushButton(box);
    c.rainbow = new QCheckBox("Rainbow (multi-colour)", box);
    setButtonColor(c.color, c.rgb);
    form->addRow("Effect", c.mode);
    form->addRow("Brightness", c.brightness);
    form->addRow("Speed", c.speed);
    form->addRow("Option / direction", c.option);
    form->addRow("Colour", c.color);
    form->addRow("", c.rainbow);
    auto* row = new QHBoxLayout;
    auto* read = button("Read from keyboard", box);
    auto* apply = button("Apply", box);
    row->addWidget(read);
    row->addWidget(apply);
    form->addRow(row);
    connect(c.color, &QPushButton::clicked, this, [this, &c] {
        QColor col = QColorDialog::getColor(QColor::fromRgb(c.rgb), this, "Colour");
        if (!col.isValid()) return;
        c.rgb = Rgb(col.rgb() & 0xFFFFFF);
        setButtonColor(c.color, c.rgb);
    });
    connect(c.mode, &QComboBox::currentIndexChanged, this, [this, &c, side] { updateLightControls(c, side); });
    connect(read, &QPushButton::clicked, this, [this, side] { readLight(side); });
    connect(apply, &QPushButton::clicked, this, [this, side] { writeLight(side); });
    return box;
}

QWidget* MainWindow::buildLightTab() {
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->addWidget(buildLightGroup("Key backlight", light_, false));
    sideGroup_ = buildLightGroup("Side light", side_, true);
    l->addWidget(sideGroup_);
    l->addStretch(1);
    return w;
}

void MainWindow::updateLightControls(LightControls& c, bool side) {
    if (!kb_) return;
    const auto& modes = side ? kb_->model().sideLightModes : kb_->model().lightModes;
    int i = c.mode->currentIndex();
    if (i < 0 || i >= int(modes.size())) return;
    const auto& m = modes[size_t(i)];
    c.speed->setEnabled(m.speed);
    c.option->setEnabled(m.options > 0);
    c.option->setRange(0, std::max(0, m.options - 1));
    c.color->setEnabled(m.color);
    c.rainbow->setEnabled(m.color);
}

void MainWindow::readLight(bool side) {
    guarded([&] {
        auto& c = side ? side_ : light_;
        const auto& modes = side ? kb_->model().sideLightModes : kb_->model().lightModes;
        auto l = side ? kb_->getSideLight() : kb_->getLight();
        if (!l) return;
        for (size_t i = 0; i < modes.size(); ++i)
            if (modes[i].code == l->mode) c.mode->setCurrentIndex(int(i));
        c.brightness->setValue(l->brightness);
        c.speed->setValue(l->speed);
        c.option->setValue(l->option);
        c.rgb = l->color;
        setButtonColor(c.color, c.rgb);
        c.rainbow->setChecked(l->rainbow);
    });
}

void MainWindow::writeLight(bool side) {
    guarded([&] {
        auto& c = side ? side_ : light_;
        const auto& modes = side ? kb_->model().sideLightModes : kb_->model().lightModes;
        int i = c.mode->currentIndex();
        if (i < 0) return;
        LightSetting l;
        l.mode = modes[size_t(i)].code;
        l.brightness = uint8_t(c.brightness->value());
        l.speed = uint8_t(c.speed->value());
        l.option = uint8_t(c.option->value());
        l.color = c.rgb;
        l.rainbow = c.rainbow->isChecked();
        side ? kb_->setSideLight(l) : kb_->setLight(l);
    });
}

// ----------------------------------------------------------------------------- key mapping
QWidget* MainWindow::buildKeysTab() {
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    auto* row = new QHBoxLayout;
    keyLayer_ = new QSpinBox(w);
    keyFn_ = new QCheckBox("Fn layer", w);
    keyOs_ = new QComboBox(w);
    keyOs_->addItems({"Windows", "macOS"});
    auto* read = button("Read", w);
    auto* write = button("Write changes", w);
    auto* reset = button("Restore defaults", w);
    row->addWidget(new QLabel("Profile:", w));
    row->addWidget(keyLayer_);
    row->addWidget(keyFn_);
    row->addWidget(new QLabel("Fn OS:", w));
    row->addWidget(keyOs_);
    row->addStretch(1);
    row->addWidget(read);
    row->addWidget(write);
    row->addWidget(reset);
    l->addLayout(row);
    keyTable_ = new QTableWidget(0, 3, w);
    keyTable_->setHorizontalHeaderLabels({"Slot", "Key", "Action (editable)"});
    keyTable_->horizontalHeader()->setStretchLastSection(true);
    keyTable_->verticalHeader()->setVisible(false);
    l->addWidget(keyTable_, 1);
    auto* help = new QLabel(
        "Actions: A, F5, LCtrl+C, consumer:VolumeUp, system:Sleep, mouse:Left, macro:N[:repeat|toggle|hold], "
        "Fn, profile:Next, Disabled, raw:aa,bb,cc,dd", w);
    help->setWordWrap(true);
    l->addWidget(help);
    connect(read, &QPushButton::clicked, this, &MainWindow::readKeys);
    connect(write, &QPushButton::clicked, this, &MainWindow::writeKeys);
    connect(reset, &QPushButton::clicked, this, [this] {
        guarded([&] {
            uint8_t layer = uint8_t(keyLayer_->value());
            uint8_t os = uint8_t(keyOs_->currentIndex());
            if (QMessageBox::question(this, "Restore defaults", "Write the factory layout to this layer?") != QMessageBox::Yes)
                return;
            keyFn_->isChecked() ? kb_->setFnMatrix(layer, os, kb_->defaultFnMatrix())
                                : kb_->setKeyMatrix(layer, kb_->defaultMatrix());
        });
        readKeys();
    });
    return w;
}

void MainWindow::readKeys() {
    guarded([&] {
        uint8_t layer = uint8_t(keyLayer_->value());
        uint8_t os = uint8_t(keyOs_->currentIndex());
        loadedMatrix_ = keyFn_->isChecked() ? kb_->getFnMatrix(layer, os) : kb_->getKeyMatrix(layer);
        keyTable_->setRowCount(0);
        for (int i = 0; i < kSlots; ++i) {
            std::string name = slotName(kb_->defaultMatrix(), i);
            KeyAction a{loadedMatrix_[i * 4], loadedMatrix_[i * 4 + 1], loadedMatrix_[i * 4 + 2], loadedMatrix_[i * 4 + 3]};
            if (name.empty() && a == KeyAction{0, 0, 0, 0}) continue;
            int r = keyTable_->rowCount();
            keyTable_->insertRow(r);
            auto* slot = new QTableWidgetItem(QString::number(i));
            slot->setFlags(slot->flags() & ~Qt::ItemIsEditable);
            auto* key = new QTableWidgetItem(QString::fromStdString(name.empty() ? "-" : name));
            key->setFlags(key->flags() & ~Qt::ItemIsEditable);
            keyTable_->setItem(r, 0, slot);
            keyTable_->setItem(r, 1, key);
            keyTable_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(actionToString(a))));
        }
    });
}

void MainWindow::writeKeys() {
    guarded([&] {
        uint8_t layer = uint8_t(keyLayer_->value());
        uint8_t os = uint8_t(keyOs_->currentIndex());
        int changed = 0;
        for (int r = 0; r < keyTable_->rowCount(); ++r) {
            int slot = keyTable_->item(r, 0)->text().toInt();
            std::string err;
            auto a = parseAction(keyTable_->item(r, 2)->text().trimmed().toStdString(), &err);
            if (!a) throw std::runtime_error("slot " + std::to_string(slot) + ": " + err);
            KeyAction old{loadedMatrix_[slot * 4], loadedMatrix_[slot * 4 + 1], loadedMatrix_[slot * 4 + 2],
                          loadedMatrix_[slot * 4 + 3]};
            if (*a == old) continue;
            keyFn_->isChecked() ? kb_->setFnKey(layer, os, uint8_t(slot), *a) : kb_->setKey(layer, uint8_t(slot), *a);
            std::copy(a->begin(), a->end(), loadedMatrix_.begin() + slot * 4);
            ++changed;
        }
        status_->setText(QString("%1 key(s) written").arg(changed));
    });
}

// ----------------------------------------------------------------------------- per-key colours
QWidget* MainWindow::buildPerKeyTab() {
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    auto* row = new QHBoxLayout;
    perKeyLayer_ = new QSpinBox(w);
    auto* read = button("Read", w);
    auto* fill = button("Fill all...", w);
    auto* write = button("Write", w);
    row->addWidget(new QLabel("Pattern:", w));
    row->addWidget(perKeyLayer_);
    row->addStretch(1);
    row->addWidget(read);
    row->addWidget(fill);
    row->addWidget(write);
    l->addLayout(row);
    perKeyTable_ = new QTableWidget(0, 2, w);
    perKeyTable_->setHorizontalHeaderLabels({"Key", "Colour (double-click)"});
    perKeyTable_->horizontalHeader()->setStretchLastSection(true);
    perKeyTable_->verticalHeader()->setVisible(false);
    perKeyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    l->addWidget(perKeyTable_, 1);
    l->addWidget(new QLabel("Shown by the \"custom\" lighting effect; its option selects the pattern.", w));

    auto redraw = [this] {
        perKeyTable_->setRowCount(0);
        if (!kb_) return;
        for (int i = 0; i < kSlots; ++i) {
            std::string name = slotName(kb_->defaultMatrix(), i);
            if (name.empty()) continue;
            int r = perKeyTable_->rowCount();
            perKeyTable_->insertRow(r);
            auto* k = new QTableWidgetItem(QString::fromStdString(name));
            k->setData(Qt::UserRole, i);
            auto* c = new QTableWidgetItem(hexColor(perKeyColors_[size_t(i)]));
            c->setBackground(QColor::fromRgb(perKeyColors_[size_t(i)]));
            perKeyTable_->setItem(r, 0, k);
            perKeyTable_->setItem(r, 1, c);
        }
    };
    connect(read, &QPushButton::clicked, this, [this, redraw] {
        readPerKey();
        redraw();
    });
    connect(write, &QPushButton::clicked, this, &MainWindow::writePerKey);
    connect(fill, &QPushButton::clicked, this, [this, redraw] {
        QColor col = QColorDialog::getColor(Qt::white, this, "Fill colour");
        if (!col.isValid() || !kb_) return;
        for (int i = 0; i < kSlots; ++i)
            perKeyColors_[size_t(i)] = slotName(kb_->defaultMatrix(), i).empty() ? 0 : Rgb(col.rgb() & 0xFFFFFF);
        redraw();
    });
    connect(perKeyTable_, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        int slot = perKeyTable_->item(r, 0)->data(Qt::UserRole).toInt();
        QColor col = QColorDialog::getColor(QColor::fromRgb(perKeyColors_[size_t(slot)]), this, "Key colour");
        if (!col.isValid()) return;
        perKeyColors_[size_t(slot)] = Rgb(col.rgb() & 0xFFFFFF);
        perKeyTable_->item(r, 1)->setText(hexColor(perKeyColors_[size_t(slot)]));
        perKeyTable_->item(r, 1)->setBackground(col);
    });
    return w;
}

void MainWindow::readPerKey() {
    guarded([&] { perKeyColors_ = kb_->getUserPicture(uint8_t(perKeyLayer_->value())); });
}

void MainWindow::writePerKey() {
    guarded([&] { kb_->setUserPicture(uint8_t(perKeyLayer_->value()), perKeyColors_); });
}

// ----------------------------------------------------------------------------- macros
QWidget* MainWindow::buildMacroTab() {
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    auto* row = new QHBoxLayout;
    macroSlot_ = new QSpinBox(w);
    macroSlot_->setRange(0, kMaxMacros - 1);
    macroRepeat_ = new QSpinBox(w);
    macroRepeat_->setRange(0, 65535);
    macroRepeat_->setValue(1);
    auto* read = button("Read", w);
    auto* write = button("Write", w);
    row->addWidget(new QLabel("Macro slot:", w));
    row->addWidget(macroSlot_);
    row->addWidget(new QLabel("Repeat:", w));
    row->addWidget(macroRepeat_);
    row->addStretch(1);
    row->addWidget(read);
    row->addWidget(write);
    l->addLayout(row);
    macroText_ = new QPlainTextEdit(w);
    macroText_->setPlaceholderText("+LCtrl/5 C/20 -LCtrl/5");
    l->addWidget(macroText_, 1);
    auto* help = new QLabel(
        "Events: +KEY (down), -KEY (up), KEY (tap), +mouse:Left / -mouse:Left, move:DX,DY. "
        "Append /MS for the delay after an event (default 10 ms). Assign the macro to a key with the action "
        "macro:N on the Key mapping tab.", w);
    help->setWordWrap(true);
    l->addWidget(help);
    connect(read, &QPushButton::clicked, this, &MainWindow::readMacro);
    connect(write, &QPushButton::clicked, this, &MainWindow::writeMacro);
    return w;
}

void MainWindow::readMacro() {
    guarded([&] {
        Macro m = kb_->getMacro(uint8_t(macroSlot_->value()));
        macroRepeat_->setValue(m.repeat);
        macroText_->setPlainText(QString::fromStdString(macroToString(m)));
    });
}

void MainWindow::writeMacro() {
    guarded([&] {
        Macro m = parseMacro(macroText_->toPlainText().toStdString(), uint16_t(macroRepeat_->value()));
        kb_->setMacro(uint8_t(macroSlot_->value()), m);
    });
}

// ----------------------------------------------------------------------------- screen
QWidget* MainWindow::buildScreenTab() {
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    auto* row = new QHBoxLayout;
    imagePath_ = new QLineEdit(w);
    auto* browse = button("Browse...", w);
    row->addWidget(new QLabel("Image / GIF:", w));
    row->addWidget(imagePath_, 1);
    row->addWidget(browse);
    l->addLayout(row);
    imagePreview_ = new QLabel(w);
    imagePreview_->setMinimumSize(240, 135);
    imagePreview_->setAlignment(Qt::AlignCenter);
    imagePreview_->setStyleSheet("background: black;");
    l->addWidget(imagePreview_);
    auto* row2 = new QHBoxLayout;
    screenSlot_ = new QSpinBox(w);
    screenSlot_->setRange(1, 5);
    gifDelay_ = new QSpinBox(w);
    gifDelay_->setRange(0, 255);
    gifDelay_->setSpecialValueText("from GIF");
    gifDelay_->setSuffix(" ms");
    auto* upload = button("Upload", w);
    row2->addWidget(new QLabel("Static image slot:", w));
    row2->addWidget(screenSlot_);
    row2->addWidget(new QLabel("GIF frame delay:", w));
    row2->addWidget(gifDelay_);
    row2->addStretch(1);
    row2->addWidget(upload);
    l->addLayout(row2);
    progress_ = new QProgressBar(w);
    progress_->setRange(0, 1000);
    l->addWidget(progress_);
    auto* row3 = new QHBoxLayout;
    auto* clock = button("Sync clock", w);
    auto* lang = button("Language switch", w);
    auto* erase = button("Erase images", w);
    sysSync_ = new QCheckBox("Send system info every second", w);
    row3->addWidget(clock);
    row3->addWidget(lang);
    row3->addWidget(erase);
    row3->addWidget(sysSync_);
    row3->addStretch(1);
    l->addLayout(row3);
    l->addStretch(1);

    connect(browse, &QPushButton::clicked, this, [this] {
        QString f = QFileDialog::getOpenFileName(this, "Image", {}, "Images (*.png *.jpg *.jpeg *.bmp *.gif *.tga)");
        if (f.isEmpty()) return;
        imagePath_->setText(f);
        QPixmap p(f);
        if (!p.isNull()) imagePreview_->setPixmap(p.scaled(240, 135, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });
    connect(upload, &QPushButton::clicked, this, &MainWindow::uploadImage);
    connect(clock, &QPushButton::clicked, this, [this] { guarded([&] { kb_->syncClock(); }); });
    connect(lang, &QPushButton::clicked, this, [this] { guarded([&] { kb_->switchScreenLanguage(true); }); });
    connect(erase, &QPushButton::clicked, this, [this] {
        if (!kb_ || QMessageBox::question(this, "Erase", "Erase all images stored on the keyboard screen?") != QMessageBox::Yes)
            return;
        guarded([&] {
            if (!kb_->eraseScreenFlash()) throw std::runtime_error("keyboard did not acknowledge the erase");
            QMessageBox::information(this, "Erase", "Erase started; it takes about 55 seconds.");
        });
    });
    connect(sysSync_, &QCheckBox::toggled, this, [this](bool on) {
        if (on) {
            sysinfo_.sample();
            sysTimer_.start(1000);
        } else {
            sysTimer_.stop();
        }
    });
    return w;
}

void MainWindow::uploadImage() {
    guarded([&] {
        LoadedImage img = loadImage(imagePath_->text().toStdString());
        const auto& m = kb_->model();
        const int W = m.screenWidth, H = m.screenHeight;
        auto prog = [this](double p) {
            progress_->setValue(int(p * 1000));
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        };
        if (img.frames.size() == 1) {
            uint8_t slot = uint8_t(screenSlot_->value() - 1);
            auto px = fitToScreen(img.frames[0], W, H);
            ScreenImage si = toScreenImage(px, W, contentRect(px, W, H));
            if (!kb_->screenBegin(slot, 1, 0, si)) throw std::runtime_error("keyboard rejected the image upload");
            kb_->screenSendFrame(slot, 1, 0, si, prog);
            return;
        }
        if (img.frames.size() > 255) throw std::runtime_error("GIF has more than 255 frames");
        int delay = gifDelay_->value() ? gifDelay_->value() : (img.delaysMs[0] > 0 ? std::min(img.delaysMs[0], 255) : 50);
        std::vector<std::vector<uint32_t>> px;
        std::vector<Rect> rects;
        for (const auto& f : img.frames) {
            px.push_back(fitToScreen(f, W, H));
            rects.push_back(contentRect(px.back(), W, H));
        }
        Rect u = unionRect(rects);
        uint8_t n = uint8_t(img.frames.size());
        std::vector<ScreenImage> frames;
        for (auto& p : px) frames.push_back(toScreenImage(p, W, u));
        if (!kb_->screenBegin(0, n, uint8_t(delay), frames[0])) throw std::runtime_error("keyboard rejected the upload");
        for (size_t i = 0; i < frames.size(); ++i)
            kb_->screenSendFrame(uint8_t(i), n, uint8_t(delay), frames[i],
                                 [&](double p) { prog((double(i) + p) / double(frames.size())); });
    });
}

void MainWindow::sendSysInfo() {
    if (!kb_) return;
    try {
        kb_->sendSystemInfo(sysinfo_.sample());
    } catch (const std::exception& e) {
        sysSync_->setChecked(false);
        QMessageBox::warning(this, "System info", e.what());
    }
}

// ----------------------------------------------------------------------------- settings
QWidget* MainWindow::buildSettingsTab() {
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    auto* box = new QGroupBox("General", w);
    auto* form = new QFormLayout(box);
    profile_ = new QSpinBox(box);
    debounce_ = new QSpinBox(box);
    debounce_->setRange(0, 255);
    debounce_->setSuffix(" ms");
    auto mk = [&](const QString& label) {
        auto* s = new QSpinBox(box);
        s->setRange(0, 65535);
        s->setSuffix(" s");
        form->addRow(label, s);
        return s;
    };
    form->addRow("Active profile", profile_);
    form->addRow("Debounce", debounce_);
    sleepBt_ = mk("Sleep after (Bluetooth)");
    sleep24_ = mk("Sleep after (2.4 GHz)");
    deepBt_ = mk("Deep sleep after (Bluetooth)");
    deep24_ = mk("Deep sleep after (2.4 GHz)");
    l->addWidget(box);
    optionsBox_ = new QGroupBox("Option flags", w);
    optionsForm_ = new QFormLayout(optionsBox_);
    l->addWidget(optionsBox_);
    auto* row = new QHBoxLayout;
    auto* read = button("Read", w);
    auto* write = button("Write", w);
    auto* reset = button("Factory reset...", w);
    row->addWidget(read);
    row->addWidget(write);
    row->addStretch(1);
    row->addWidget(reset);
    l->addLayout(row);
    l->addStretch(1);
    connect(read, &QPushButton::clicked, this, &MainWindow::readSettings);
    connect(write, &QPushButton::clicked, this, &MainWindow::writeSettings);
    connect(reset, &QPushButton::clicked, this, [this] {
        if (!kb_ || QMessageBox::question(this, "Factory reset", "Reset the keyboard to factory settings?") != QMessageBox::Yes)
            return;
        guarded([&] { kb_->factoryReset(); });
        readSettings();
    });
    return w;
}

void MainWindow::readSettings() {
    guarded([&] {
        if (auto p = kb_->getProfile()) profile_->setValue(*p);
        if (auto d = kb_->getDebounce()) debounce_->setValue(*d);
        if (auto s = kb_->getSleep()) {
            sleepBt_->setValue(s->bluetooth);
            sleep24_->setValue(s->wireless24);
            deepBt_->setValue(s->deepBluetooth);
            deep24_->setValue(s->deepWireless24);
        }
        auto opts = kb_->getOptions();
        while (optionsForm_->rowCount() > 0) optionsForm_->removeRow(0);
        optionEdits_.clear();
        for (const auto& [k, v] : opts) {
            auto* s = new QSpinBox(optionsBox_);
            s->setRange(0, 255);
            s->setValue(v);
            s->setProperty("loaded", v);
            optionsForm_->addRow(QString::fromStdString(k), s);
            optionEdits_[k] = s;
        }
    });
}

void MainWindow::writeSettings() {
    guarded([&] {
        const auto& m = kb_->model();
        if (auto p = kb_->getProfile(); !p || *p != profile_->value()) kb_->setProfile(uint8_t(profile_->value()));
        if (auto d = kb_->getDebounce(); !d || *d != debounce_->value()) kb_->setDebounce(uint8_t(debounce_->value()));
        auto inRange = [](int v, int lo, int hi) { return v >= lo && v <= hi; };
        SleepTimes t{uint16_t(sleepBt_->value()), uint16_t(sleep24_->value()), uint16_t(deepBt_->value()),
                     uint16_t(deep24_->value())};
        if (!inRange(t.bluetooth, m.sleepMin, m.sleepMax) || !inRange(t.wireless24, m.sleepMin, m.sleepMax) ||
            !inRange(t.deepBluetooth, m.deepSleepMin, m.deepSleepMax) ||
            !inRange(t.deepWireless24, m.deepSleepMin, m.deepSleepMax))
            throw std::runtime_error("sleep times must be " + std::to_string(m.sleepMin) + "-" + std::to_string(m.sleepMax) +
                                     " s, deep sleep " + std::to_string(m.deepSleepMin) + "-" +
                                     std::to_string(m.deepSleepMax) + " s");
        auto cur = kb_->getSleep();
        if (!cur || cur->bluetooth != t.bluetooth || cur->wireless24 != t.wireless24 ||
            cur->deepBluetooth != t.deepBluetooth || cur->deepWireless24 != t.deepWireless24)
            kb_->setSleep(t);
        std::map<std::string, int> changes;
        for (const auto& [k, s] : optionEdits_)
            if (s->value() != s->property("loaded").toInt()) changes[k] = s->value();
        if (!changes.empty()) kb_->setOptions(changes);
    });
    readSettings();
}

void MainWindow::populateForModel() {
    const auto& m = kb_->model();
    light_.mode->clear();
    for (const auto& lm : m.lightModes) light_.mode->addItem(QString::fromStdString(lm.name));
    light_.speed->setRange(0, m.maxSpeed);
    side_.mode->clear();
    for (const auto& lm : m.sideLightModes) side_.mode->addItem(QString::fromStdString(lm.name));
    side_.speed->setRange(0, m.maxSideSpeed);
    sideGroup_->setVisible(!m.sideLightModes.empty());
    keyLayer_->setRange(0, m.layers - 1);
    keyOs_->setEnabled(m.chip == Chip::YC3123);
    profile_->setRange(0, m.layers - 1);
    perKeyLayer_->setRange(0, m.chip == Chip::YC3123 ? 4 : 0);
    screenSlot_->setRange(1, m.screenSlots);
    sysSync_->setEnabled(true);
    updateLightControls(light_, false);
    updateLightControls(side_, true);
}
