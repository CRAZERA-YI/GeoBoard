# ---------------------------------------------------------------------------
# 文件读写往返自测（06 阶段）—— 独立工程
#
# 覆盖"保存后能否正确打开"这条用户数据安全路径：把全部图元类型与全部测量类型
# 存盘再读回，逐项比对；另验证损坏文件被拒绝且不破坏原有文档。
#
# 为什么需要 QtWidgets：JsonStore 通过 Shape/GeoDocument 间接用到 QUndoCommand
# 所在的模块无关，但 GeoDocument 继承 QObject；这里显式声明 widgets 是为了与
# 主工程保持同一套 Qt 模块，避免"测试能编译、主程序不能"的假象。
# 注意：几何内核本身零界面依赖这件事，由 tests/GeometrySmoke.pro（只用 QtCore）证明。
#
# 用法：
#   scripts\build.bat json     （见 scripts/build.bat）
# 或手工：
#   mkdir build-json && cd build-json
#   qmake ../tests/JsonSmoke.pro && nmake
#   release\json_smoke.exe
# ---------------------------------------------------------------------------

TEMPLATE = app
TARGET   = json_smoke

QT += core

CONFIG += console c++11 warn_on
CONFIG -= app_bundle

DEFINES += QT_DEPRECATED_WARNINGS

msvc {
    QMAKE_CFLAGS   += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

INCLUDEPATH += $$PWD/../src

SOURCES += \
    JsonSmoke.cpp \
    ../src/model/GeometryMath.cpp \
    ../src/model/Shape.cpp \
    ../src/model/GeoDocument.cpp \
    ../src/service/JsonStore.cpp

HEADERS += \
    ../src/model/GeometryMath.h \
    ../src/model/Shape.h \
    ../src/model/GeoDocument.h \
    ../src/service/JsonStore.h
