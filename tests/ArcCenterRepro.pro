# ---------------------------------------------------------------------------
# 圆心式圆弧的现场诊断工具（临时工程）
#
# 用途：把"圆心 + 两点画圆弧"的真人操作逐事件回放，并打印每一步的状态机与
#       文档内容，用来定位"点了没反应"到底发生在哪一步。
#
# 用法：mkdir build-repro && cd build-repro
#       qmake ../tests/ArcCenterRepro.pro && nmake && release\arc_repro.exe
# ---------------------------------------------------------------------------

TEMPLATE = app
TARGET   = arc_repro

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
    ArcCenterRepro.cpp \
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
