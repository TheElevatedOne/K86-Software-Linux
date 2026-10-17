#include <QApplication>
#include <QCommandLineParser>
#include <QTabWidget>
#include <QTimer>

#include "mainwindow.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("k86-gui");
    QCommandLineParser p;
    p.setApplicationDescription("Attack Shark K86 configuration tool");
    p.addHelpOption();
    QCommandLineOption dry("dry-run", "Run without hardware against a simulated K86 (v1 = YC3121, v2 = YC3123).", "model");
    QCommandLineOption shot("screenshot", "Save a PNG of every tab as <prefix>-N.png and exit.", "prefix");
    p.addOption(dry);
    p.addOption(shot);
    p.process(app);
    MainWindow w(p.isSet(dry) ? p.value(dry) : QString());
    w.show();
    if (p.isSet(shot)) {
        QString prefix = p.value(shot);
        QTimer::singleShot(200, &w, [&w, prefix] {
            auto* tabs = w.findChild<QTabWidget*>();
            for (int i = 0; tabs && i < tabs->count(); ++i) {
                tabs->setCurrentIndex(i);
                QApplication::processEvents();
                w.grab().save(QString("%1-%2.png").arg(prefix).arg(i));
            }
            QApplication::quit();
        });
    }
    return app.exec();
}
