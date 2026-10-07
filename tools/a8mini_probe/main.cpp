#include "A8VideoPipeline.h"
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QCommandLineParser>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDir>
#include <QDebug>

int main(int argc, char *argv[]) {
    bool software = qgetenv("QT_QUICK_BACKEND") == "software";
    for (int i = 1; i < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == "--software") software = true;
    if (software) qputenv("QT_QUICK_BACKEND", "software");
    else {
        QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
        QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    }
    if (!software) gst_init(nullptr, nullptr);
    QGuiApplication app(argc, argv);
    app.setApplicationName("a8mini_probe");
    app.setApplicationVersion("0.1.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("Qt5 / GStreamer A8 mini video Probe");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addOptions({
        {{"u", "url"}, "RTSP address", "url", "rtsp://192.168.2.113:8554/a8mini"},
        {"credentials", "JSON file containing username and password", "file", "artifacts/client-credentials.json"},
        {"test", "Play a test pattern"},
        {"file", "Play a local video file", "path"},
        {"software", "Use software UI; disable GL video"},
        {"latency", "RTSP latency in milliseconds", "ms", "300"},
        {"seconds", "Quit after N seconds and fail if no frames were rendered", "seconds"},
        {"report", "Write a credential-free JSON report on timed exit", "file"},
        {"screenshot", "Save the Qt window at timed exit", "file"},
        {"restart-after", "Restart this client's pipeline after N seconds", "seconds"}
    });
    parser.process(app);
    bool latencyOk = false;
    int latency = parser.value("latency").toInt(&latencyOk);
    if (!latencyOk || latency < 80 || latency > 2000) {
        qCritical() << "--latency must be between 80 and 2000"; return 2;
    }
    QJsonObject credentials;
    QFile credentialFile(parser.value("credentials"));
    if (credentialFile.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        auto document = QJsonDocument::fromJson(credentialFile.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            qCritical() << "Invalid credentials JSON"; return 2;
        }
        credentials = document.object();
    }
    // Must precede engine.load(), because the plugin registers its QML type.
    A8VideoPipeline video(!software);
    QJsonArray events;
    QString previousState, previousError;
    // Disconnect the diagnostic lambda before its captured locals are destroyed.
    QObject eventContext;
    QObject::connect(&video, &A8VideoPipeline::changed, &eventContext, [&] {
        if (previousState == video.state() && previousError == video.lastError()) return;
        previousState = video.state(); previousError = video.lastError();
        events.append(QJsonObject{{"state", previousState}, {"error", previousError},
                                  {"time", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
        qInfo().noquote() << previousState << previousError;
    });
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("a8Video", &video);
    engine.rootContext()->setContextProperty("initialUrl", parser.value("url"));
    engine.rootContext()->setContextProperty("initialUsername", credentials.value("username").toString("a8viewer"));
    engine.rootContext()->setContextProperty("initialPassword", credentials.value("password").toString());
    engine.rootContext()->setContextProperty("initialLatency", latency);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 2;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window) return 2;
    auto *item = window->findChild<QQuickItem *>("videoSurface");
    if (video.available() && !item) {
        qCritical() << "GstGLVideoItem could not be loaded; check Qt5/qmlgl compatibility"; return 2;
    }
    if (item) video.attach(item);
    auto start = [&] {
        if (parser.isSet("test")) video.playTest();
        else if (parser.isSet("file")) video.playFile(parser.value("file"));
        else video.play(parser.value("url"), credentials.value("username").toString("a8viewer"),
                        credentials.value("password").toString(), latency);
    };
    QTimer::singleShot(0, &video, start);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &video, &A8VideoPipeline::stop);
    if (parser.isSet("restart-after")) {
        bool ok = false; int seconds = parser.value("restart-after").toInt(&ok);
        if (!ok || seconds < 1 || seconds > 86400) return 2;
        QTimer::singleShot(seconds * 1000, &video, start);
    }
    if (parser.isSet("seconds")) {
        bool ok = false; int seconds = parser.value("seconds").toInt(&ok);
        if (!ok || seconds < 1 || seconds > 86400) return 2;
        QTimer::singleShot(seconds * 1000, &app, [&] {
            bool passed = software ? video.state() == "DISABLED"
                : video.renderedFrames() > 0 && (video.state() == "PLAYING"
                    || (parser.isSet("file") && video.state() == "FINISHED"));
            QJsonObject report {
                {"timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                {"qt", qVersion()}, {"gstreamer", QString::fromLatin1(gst_version_string())},
                {"state", video.state()}, {"error", video.lastError()},
                {"source", parser.isSet("test") ? "test" : parser.isSet("file") ? "file" : "rtsp"},
                {"latency_ms", latency}, {"width", video.width()}, {"height", video.height()},
                {"fps", video.fps()}, {"rendered", double(video.renderedFrames())},
                {"dropped", double(video.droppedFrames())},
                {"reconnects", video.reconnectCount()}, {"decoder", video.decoderName()},
                {"events", events}, {"passed", passed}
            };
            auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
            if (parser.isSet("report")) {
                QFile file(parser.value("report"));
                if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
                    qCritical() << "Cannot write report"; passed = false;
                }
            }
            if (parser.isSet("screenshot") && !window->grabWindow().save(parser.value("screenshot"))) {
                qCritical() << "Cannot save screenshot"; passed = false;
            }
            qInfo().noquote() << bytes;
            app.exit(passed ? 0 : 1);
        });
    }
    int result = app.exec();
    video.stop();
    // gst_deinit() is intentionally omitted: Qt and the plugin still own GL resources.
    return result;
}
