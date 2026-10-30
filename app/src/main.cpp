// FrameKeeper desktop app entry point (RFC-0011).
#include "AppController.hpp"
#include "ImageProvider.hpp"
#include "SelfTest.hpp"
#include "Translator.hpp"

#include "fk/version.hpp"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>

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
    }
    // The self-test changes settings (export folder, formats); keep them away from the user's own settings.
    if (selfTest && selfTestDir && qEnvironmentVariableIsEmpty("FK_SETTINGS_DIR"))
        qputenv("FK_SETTINGS_DIR", QByteArray(selfTestDir) + "/settings");

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("FrameKeeper"));
    QGuiApplication::setOrganizationName(QStringLiteral("FrameKeeper"));
    QGuiApplication::setApplicationVersion(QString::fromStdString(fk::productVersion()));
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
        QStringLiteral("With --self-test: reproduce a reported flow (relink, count, upright)."),
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
