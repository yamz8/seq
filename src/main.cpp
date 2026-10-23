// seqview — open a FASTA or FASTQ file fast, look through its sequences, and get the stats.

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>
#include <cstdio>

#include "backend.h"
#include "chart.h"
#include "cli.h"
#include "models.h"
#include "theme.h"
#include "translator.h"

int main(int argc, char *argv[]) {
    // Commands and help draw no window, so they must not need a display, even
    // where the desktop exports QT_QPA_PLATFORM=wayland. Bare seqview prints
    // help, as a command line tool should; the launcher says seqview open.
    const QString first = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const bool command = argc == 1 || isCliCommand(first) || first == "--version" || first == "-v";
    if (command)
        qputenv("QT_QPA_PLATFORM", "offscreen");
    // seqview themes itself, so Qt's GTK platform theme only adds GTK's
    // accessibility bus and its warnings, even to commands.
    qputenv("QT_QPA_PLATFORMTHEME", "generic");

    QGuiApplication app(argc, argv);
    app.setApplicationName("seqview");
    app.setApplicationVersion("0.1.0");

    if (first == "--version" || first == "-v") {
        printf("seqview %s\n", qPrintable(app.applicationVersion()));
        return 0;
    }
    if (command)
        return runCli(app.arguments());

    // seqview open [file], or seqview <file> from a file manager.
    QStringList arguments = app.arguments().mid(1);
    if (arguments.value(0) == "open")
        arguments.removeFirst();
    const QString file = arguments.value(0);
    if (!file.isEmpty() && !QFileInfo(file).isFile() && !QUrl(file).isLocalFile()) {
        fprintf(stderr, "%s: no such file\n\nUsage: seqview <command> [options]\n\n%s\n", qPrintable(file),
                qPrintable(cliSummary()));
        return 1;
    }

    // Matches the window to seqview.desktop, so the compositor (Wayland
    // app_id) and the launcher pick up the installed icon.
    app.setDesktopFileName("seqview");
    app.setWindowIcon(QIcon::fromTheme("seqview"));

    QQuickStyle::setStyle("Material");
    // The compact Material controls suit a desktop window.
    qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
    qmlRegisterType<ChartItem>("Seq", 1, 0, "BarChart");
    qmlRegisterType<SequenceLine>("Seq", 1, 0, "SequenceLine");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend;

    QQmlApplicationEngine engine;
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    if (!file.isEmpty())
        backend.openUrl(QUrl::fromUserInput(file, QDir::currentPath(), QUrl::AssumeLocalFile));

    return app.exec();
}
