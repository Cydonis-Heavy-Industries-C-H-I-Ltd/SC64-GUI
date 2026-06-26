#include "sc64controller.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationName("SC64 File Transfer");
    app.setOrganizationName("sc64gui");

    QQmlApplicationEngine engine;

    Sc64Controller controller;
    engine.rootContext()->setContextProperty("sc64", &controller);

    const QUrl url(QStringLiteral("qrc:/Sc64Gui/qml/Main.qml"));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
