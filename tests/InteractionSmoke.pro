# ---------------------------------------------------------------------------
# 交互自测（06 阶段）—— 独立工程
#
# 用合成的 QMouseEvent 驱动真实窗口，验证"点两下 / 按住拖动 / 双击后确认"
# 三种作图方式确实产生了图元，并验证撤销栈有效。
#
# 存在的理由：用户报告过一次"画圆失灵"的回归，靠读代码无法定位；
# 这类缺陷必须用可重复的自动化事件序列守住。
#
# 用法（见 scripts/build.bat 的 interact 动作）：
#   mkdir build-interact && cd build-interact
#   qmake ../tests/InteractionSmoke.pro && nmake
#   release\interaction_smoke.exe
# ---------------------------------------------------------------------------

TEMPLATE = app
TARGET   = interaction_smoke

QT += core gui widgets svg

CONFIG += console c++11 warn_on
CONFIG -= app_bundle

DEFINES += QT_DEPRECATED_WARNINGS

# 交互自测要链接完整的主程序（除 main.cpp 外），因此也要带 svg 模块——
# 主程序启用了"导出 SVG"，缺了它会在链接期报 SceneExporter 的未解析符号。
# 注意：这里用的是**源文件**而不是主程序的 exe，这样断言的引用与主程序完全一致。

msvc {
    QMAKE_CFLAGS   += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

INCLUDEPATH += $$PWD/../src

SOURCES += \
    InteractionSmoke.cpp \
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
