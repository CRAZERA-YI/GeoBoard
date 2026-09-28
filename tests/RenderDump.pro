# ---------------------------------------------------------------------------
# 离屏渲染出图 + 导出验证（06 阶段）—— 独立工程
#
# 两件事：
#   1. 把 CanvasWidget 的真实 paintEvent 画到一个 PNG 上，用于验证"圆的抗锯齿"
#      这类**只能用眼睛判断**的改动；
#   2. 顺带调用 SceneExporter 真的导出一次 PNG 与 SVG，并检查产物是否可读——
#      导出的核心路径与屏幕显示共用同一套绘制代码，因此这一步能有效守住导出功能。
#
# 用法（见 scripts/build.bat 的 render 动作）：
#   mkdir build-render && cd build-render
#   qmake ../tests/RenderDump.pro && nmake
#   release\render_dump.exe ..\docs\screenshot-render.png
# ---------------------------------------------------------------------------

TEMPLATE = app
TARGET   = render_dump

QT += core gui widgets svg

CONFIG += console c++11 warn_on
CONFIG -= app_bundle

DEFINES += QT_DEPRECATED_WARNINGS

msvc {
    QMAKE_CFLAGS   += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

INCLUDEPATH += $$PWD/../src

SOURCES += \
    RenderDump.cpp \
    ../src/model/GeometryMath.cpp \
    ../src/model/Shape.cpp \
    ../src/model/GeoDocument.cpp \
    ../src/model/Camera.cpp \
    ../src/controller/ToolId.cpp \
    ../src/view/CanvasWidget.cpp \
    ../src/view/SceneExporter.cpp

HEADERS += \
    ../src/model/GeometryMath.h \
    ../src/model/Shape.h \
    ../src/model/GeoDocument.h \
    ../src/model/Camera.h \
    ../src/controller/ToolId.h \
    ../src/view/CanvasWidget.h \
    ../src/view/SceneExporter.h
