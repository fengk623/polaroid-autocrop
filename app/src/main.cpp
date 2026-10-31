// FrameKeeper desktop app entry point (RFC-0011).
#include "AppController.hpp"
#include "ImageProvider.hpp"
#include "SelfTest.hpp"
#include "Translator.hpp"

#include "fk/version.hpp"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <cstdio>
#include <cstring>

int main(int argc, char* argv[]) {
    // Self-test runs headless (offscreen platform, software scene graph: no GPU needed in CI) unless --live
    // is given, which keeps the real window and graphics backend for captures on a person's machine
    // (HV-02/03).
    bool selfTest = false, live = false;
    const char* selfTestDir = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--self-test") == 0) {
            selfTest = true;
            if (i + 1 < argc) selfTestDir = argv[i + 1];
        }
        if (std::strcmp(argv[i], "--live") == 0) live = true;
    }
    if (selfTest && !live) {
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
#ifdef Q_OS_WIN
        // FK-009: on Windows the offscreen platform reads fonts with FreeType from QT_QPA_FONTDIR (else Qt's
        // own lib/fonts, which a Qt install does not have): without this the self-test drew boxes and
        // measured text with fallback metrics far wider than any real font. Point it at the system's fonts.
        if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) {
            const QByteArray windir = qgetenv("WINDIR");
            qputenv("QT_QPA_FONTDIR", (windir.isEmpty() ? QByteArray("C:\\Windows") : windir) + "\\Fonts");
        }
#endif
    }
    // The self-test changes settings (export folder, formats); keep them away from the user's own settings.
    if (selfTest && selfTestDir && qEnvironmentVariableIsEmpty("FK_SETTINGS_DIR"))
        qputenv("FK_SETTINGS_DIR", QByteArray(selfTestDir) + "/settings");

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("FrameKeeper"));
#ifdef Q_OS_WIN
    // FK-009: the offscreen platform's FreeType font database has no font fallback, so Chinese text in the
    // default Latin font draws boxes there (a real window falls back to Microsoft YaHei). Give the self-test
    // the same fallback order Windows uses for the UI font. That database reads only .ttf/.otf files from
    // the font folder, and Windows' Chinese UI fonts are collections (.ttc), so they are added here.
    if (selfTest && !live) {
        const QString fontDir = QString::fromLocal8Bit(qgetenv("QT_QPA_FONTDIR"));
        QStringList added;
        for (const char* file : {"msyh.ttc", "msyhbd.ttc", "simsun.ttc"}) {
            const QString path = QDir(fontDir).filePath(QLatin1String(file));
            if (QFile::exists(path) && QFontDatabase::addApplicationFont(path) >= 0)
                added << QLatin1String(file);
        }
        const QString report = added.isEmpty() ? QStringLiteral("no Chinese font collection in ") + fontDir
                                               : QStringLiteral("added ") + added.join(QStringLiteral(", "));
        std::fprintf(stderr, "self-test fonts: %s\n", qPrintable(report));
        QFont f = QGuiApplication::font();
        f.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Microsoft YaHei UI"),
                       QStringLiteral("Microsoft YaHei"), QStringLiteral("SimSun")});
        QGuiApplication::setFont(f);
    }
#endif
    QGuiApplication::setOrganizationName(QStringLiteral("FrameKeeper"));
    QGuiApplication::setApplicationVersion(
        QString::fromStdString(fk::productVersion()) + QStringLiteral(" (commit ") +
        QString::fromStdString(fk::buildCommit()) + QStringLiteral(", build ") +
        QString::fromStdString(fk::buildId()) + QStringLiteral(")"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/FrameKeeper/resources/icon.png")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("FrameKeeper — instant-print scans: crop, edit round trip, frame"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption selfTestOpt(QStringLiteral("self-test"),
                                   QStringLiteral("Run the self-test, writing results into <dir>."),
                                   QStringLiteral("dir"));
    QCommandLineOption scanOpt(QStringLiteral("scan"), QStringLiteral("Scan used by the self-test."),
                               QStringLiteral("tif"));
    QCommandLineOption liveOpt(QStringLiteral("live"),
                               QStringLiteral("Run the self-test in a real window with the GPU backend."));
    parser.addOption(selfTestOpt);
    parser.addOption(scanOpt);
    parser.addOption(liveOpt);
    QCommandLineOption scenarioOpt(
        QStringLiteral("scenario"),
        QStringLiteral("With --self-test: reproduce a reported flow (relink, count, upright, miniswhite) or "
                       "measure memory over a batch (memory; --scan <directory>)."),
        QStringLiteral("name"));
    parser.addOption(scenarioOpt);
    parser.addPositionalArgument(QStringLiteral("files"),
                                 QStringLiteral("Scans (.tif) or a project (.fkproj) to open."));
    parser.process(app);

    fkapp::AppController controller;

    // language: setting, else the system locale
    fkapp::JsonTranslator translator;
    QString lang = controller.settings().value(QStringLiteral("language")).toString();
    if (lang == QLatin1String("system") || lang.isEmpty())
        lang = QLocale::system().name().startsWith(QLatin1String("zh")) ? QStringLiteral("zh_CN")
                                                                        : QStringLiteral("en");
    if (lang == QLatin1String("zh_CN") &&
        translator.loadJson(QStringLiteral(":/qt/qml/FrameKeeper/i18n/zh_CN.json"))) {
        QCoreApplication::installTranslator(&translator);
        controller.newProject(); // the empty project was named before the translator existed
    }

    qmlRegisterSingletonInstance("FrameKeeper.Core", 1, 0, "App", &controller);
    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("fk"), new fkapp::ThumbProvider(&controller));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(2); },
        Qt::QueuedConnection);
    engine.loadFromModule("FrameKeeper", "Main");
    if (engine.rootObjects().isEmpty()) return 2;
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());

    // FK-011: the display profile follows the window's monitor — another screen, screens added or removed,
    // and every 5 s for an ICC changed in the system settings (Windows sends no notification for that)
    if (window) {
        auto refreshDisplay = [&controller, window] { controller.refreshDisplayProfile(window->screen()); };
        refreshDisplay();
        QObject::connect(window, &QWindow::screenChanged, &controller, refreshDisplay);
        QObject::connect(&app, &QGuiApplication::screenAdded, &controller, refreshDisplay);
        QObject::connect(&app, &QGuiApplication::screenRemoved, &controller, refreshDisplay);
        QObject::connect(&app, &QGuiApplication::primaryScreenChanged, &controller, refreshDisplay);
        auto* displayPoll = new QTimer(&controller);
        displayPoll->setInterval(5000);
        QObject::connect(displayPoll, &QTimer::timeout, &controller, refreshDisplay);
        displayPoll->start();
    }

    if (parser.isSet(selfTestOpt)) {
        fkapp::SelfTest t(&controller, window, parser.value(selfTestOpt), parser.value(scanOpt));
        const int rc = parser.isSet(scenarioOpt) ? t.runScenario(parser.value(scenarioOpt)) : t.run();
        std::fprintf(stderr, "self-test %s\n", rc == 0 ? "passed" : "FAILED");
        return rc;
    }

    // files given on the command line (or by the OS file association)
    QVariantList scans;
    for (const QString& f : parser.positionalArguments()) {
        if (f.endsWith(QStringLiteral(".fkproj")))
            controller.openProject(f);
        else
            scans << f;
    }
    if (!scans.isEmpty()) controller.importScans(scans);
    return app.exec();
}
