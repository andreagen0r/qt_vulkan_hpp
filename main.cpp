#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QtQuick/QQuickView>

#include <string_view>

//#define VULKAN_HPP_NO_CONSTRUCTORS


int main( int argc, char** argv ) {

    using namespace std::literals;

#if ( 1 )
    QGuiApplication app( argc, argv );

    QQuickWindow::setGraphicsApi( QSGRendererInterface::Vulkan );

    QQmlApplicationEngine engine;

    const QUrl url( u"qrc:/main.qml"_qs );
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, &app,
        [url]( QObject* obj, const QUrl& objUrl ) {
            if ( !obj && url == objUrl )
                QCoreApplication::exit( -1 );
        },
        Qt::QueuedConnection );
    engine.load( url );

    return app.exec();
#else
    qone::vkr::Engine engine;

#endif
}
