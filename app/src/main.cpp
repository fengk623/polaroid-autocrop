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
    // Self-test runs headless: offscreen platform and the software scene graph (no GPU needed in CI).
    bool selfTest = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--self-test") == 0) selfTest = true;
    if (selfTest) {
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

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
                                   QStringLiteral("Run the offscreen self-test into <dir>."),
                                   QStringLiteral("dir"));
    QCommandLineOption scanOpt(QStringLiteral("scan"), QStringLiteral("Scan used by the self-test."),
                               QStringLiteral("tif"));
    parser.addOption(selfTestOpt);
    parser.addOption(scanOpt);
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
        const int rc = t.run();
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
