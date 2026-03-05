#include "mainwindow.h"
#include "crash.h"

#include <QApplication>
#include <QIcon>
#include <cstdio>
#include <cstdlib>

static void qt_msg_handler(QtMsgType type,
                            const QMessageLogContext & /*ctx*/,
                            const QString &msg)
{
    fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
    if (type == QtFatalMsg)
        abort();
}

int main(int argc, char *argv[])
{
    install_crash_handler(argv[0]);
    qInstallMessageHandler(qt_msg_handler);

    QApplication app(argc, argv);
    app.setApplicationName("qrazercfg");
    app.setApplicationDisplayName("Razer Device Configuration");
    app.setWindowIcon(QIcon::fromTheme("razercfg"));

    MainWindow w;
    w.show();
    return app.exec();
}
