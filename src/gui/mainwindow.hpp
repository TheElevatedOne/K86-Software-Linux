#pragma once

#include <QMainWindow>
#include <QTimer>
#include <array>
#include <map>
#include <memory>

#include "discovery.hpp"
#include "keyboard.hpp"
#include "sysinfo.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QFormLayout;
class QWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // dryRunModel: empty for real hardware, "v1"/"v2" to run against a fake device.
    explicit MainWindow(const QString& dryRunModel = {}, QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    struct LightControls {
        QComboBox* mode = nullptr;
        QSlider* brightness = nullptr;
        QSlider* speed = nullptr;
        QSpinBox* option = nullptr;
        QPushButton* color = nullptr;
        QCheckBox* rainbow = nullptr;
        k86::Rgb rgb = 0xFF0000;
    };

    QWidget* buildLightTab();
    QWidget* buildLightGroup(const QString& title, LightControls& c, bool side);
    QWidget* buildKeysTab();
    QWidget* buildPerKeyTab();
    QWidget* buildMacroTab();
    QWidget* buildScreenTab();
    QWidget* buildSettingsTab();

    void refreshDevices();
    void connectDevice();
    void populateForModel();
    bool requireKeyboard();
    template <typename F>
    void guarded(F&& f);

    void readLight(bool side);
    void writeLight(bool side);
    void updateLightControls(LightControls& c, bool side);
    void readKeys();
    void writeKeys();
    void readPerKey();
    void writePerKey();
    void readMacro();
    void writeMacro();
    void uploadImage();
    void readSettings();
    void writeSettings();
    void sendSysInfo();

    QString dryRun_;
    std::unique_ptr<k86::Transport> transport_;
    std::unique_ptr<k86::Keyboard> kb_;
    k86::SystemInfoCollector sysinfo_;
    QTimer sysTimer_;

    QComboBox* deviceCombo_ = nullptr;
    QLabel* status_ = nullptr;
    QTabWidget* tabs_ = nullptr;

    LightControls light_, side_;
    QWidget* sideGroup_ = nullptr;

    QSpinBox* keyLayer_ = nullptr;
    QCheckBox* keyFn_ = nullptr;
    QComboBox* keyOs_ = nullptr;
    QTableWidget* keyTable_ = nullptr;
    k86::KeyMatrix loadedMatrix_{};

    QSpinBox* perKeyLayer_ = nullptr;
    QTableWidget* perKeyTable_ = nullptr;
    std::array<k86::Rgb, k86::kSlots> perKeyColors_{};

    QSpinBox* macroSlot_ = nullptr;
    QSpinBox* macroRepeat_ = nullptr;
    QPlainTextEdit* macroText_ = nullptr;

    QLineEdit* imagePath_ = nullptr;
    QLabel* imagePreview_ = nullptr;
    QSpinBox* screenSlot_ = nullptr;
    QSpinBox* gifDelay_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QCheckBox* sysSync_ = nullptr;

    QSpinBox* profile_ = nullptr;
    QSpinBox* debounce_ = nullptr;
    QSpinBox* sleepBt_ = nullptr;
    QSpinBox* sleep24_ = nullptr;
    QSpinBox* deepBt_ = nullptr;
    QSpinBox* deep24_ = nullptr;
    QFormLayout* optionsForm_ = nullptr;
    QWidget* optionsBox_ = nullptr;
    std::map<std::string, QSpinBox*> optionEdits_;
};
