#include "main_window.h"

#include <QApplication>
#include <QStyleFactory>
#include <QDir>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    // Set application properties
    QApplication::setApplicationName("Qt Spy Inspector");
    QApplication::setApplicationVersion("2.0");
    QApplication::setOrganizationName("Qt Spy");
    QApplication::setApplicationDisplayName("Qt Spy Inspector");
    
    // Set a modern style if available
    const QStringList availableStyles = QStyleFactory::keys();
    if (availableStyles.contains("Fusion")) {
        QApplication::setStyle("Fusion");
    }
    
    // Create and show main window
    qt_spy::MainWindow window;
    window.show();
    
    return app.exec();
}
