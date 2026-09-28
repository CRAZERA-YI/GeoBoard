# ---------------------------------------------------------------------------
# 主窗口整窗截图（含停靠面板、工具栏、状态栏）—— 独立工程
#
# 用途：自动化断言验证不了"布局看起来对不对"（面板会不会挤掉画布、工具栏会不会换行）。
#       本工具把主窗口离屏渲染成 PNG，供人工核对；不需要可见窗口，也不受屏幕 DPI 影响。
#
# 用法（见 scripts/build.bat 的 shot 动作）：
#   mkdir build-shot && cd build-shot
#   qmake ../tests/WindowShot.pro && nmake
#   release\window_shot.exe ..\docs\screenshot-window.png
# ---------------------------------------------------------------------------

TEMPLATE = app
TARGET   = window_shot

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
    WindowShot.cpp \
    ../src/model/GeometryMath.cpp \
    ../src/model/Shape.cpp \
    ../src/model/GeoDocument.cpp \
    ../src/model/Camera.cpp \
    ../src/model/Commands.cpp \
    ../src/service/JsonStore.cpp \
    ../src/controller/ToolId.cpp \
    ../src/controller/MainController.cpp \
    ../src/view/CanvasWidget.cpp \
    ../src/view/SceneExporter.cpp \
    ../src/view/ShapeListPanel.cpp \
    ../src/view/GeoBoardMainWindow.cpp

HEADERS += \
    ../src/version.h \
    ../src/model/GeometryMath.h \
    ../src/model/Shape.h \
    ../src/model/GeoDocument.h \
    ../src/model/Camera.h \
    ../src/model/Commands.h \
    ../src/service/JsonStore.h \
    ../src/controller/ToolId.h \
    ../src/controller/MainController.h \
    ../src/view/CanvasWidget.h \
    ../src/view/SceneExporter.h \
    ../src/view/ShapeListPanel.h \
    ../src/view/GeoBoardMainWindow.h
