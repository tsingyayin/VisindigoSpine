#include <QApplication>
#include <QSurfaceFormat>
#include "demo.h"

int main(int argc, char *argv[])
{
    // QOpenGLWidget 透明背景需要带 alpha 通道的 GL 帧缓冲
    QSurfaceFormat fmt;
    fmt.setAlphaBufferSize(8);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    DemoWindow w;
    w.show();
    return app.exec();
}
