# ---------------------------------------------------------------------------
# 几何引擎自测与性能基准（06 阶段产物）—— 独立工程
#
# 这是一个**无 GUI** 的控制台工程：只编译 model 层的几何代码 + 测试用例。
# 它能编译通过本身就证明了一件事：model 层没有混入界面依赖
# （见 workflow/stages/03_architecture/structure_reference.md 第二节的判据）。
#
# 为什么是**独立工程**而不是主 GeoBoard.pro 里的第二个目标：
#   同一个 .pro 里的两个 app 目标会把 obj 都写进同一个 release/ 目录，
#   于是两边各自的 main.cpp 会撞成重定义：
#       geometry_smoke.obj : error LNK2005: main 已经在 main.obj 中定义
#       fatal error LNK1169: 找到一个或多个多重定义的符号
#   拆成独立工程后，它拥有自己的构建目录，两边互不干扰。
#
# 用法（详细步骤见 docs/test.md）——在项目内的独立构建目录里构建：
#   mkdir build-test && cd build-test
#   qmake ../tests/GeometrySmoke.pro && nmake
#   release\geometry_smoke.exe
# ---------------------------------------------------------------------------

TEMPLATE = app
TARGET   = geometry_smoke

QT += core

CONFIG += console c++11 warn_on
CONFIG -= app_bundle

DEFINES += QT_DEPRECATED_WARNINGS

# 刻意**不**链接 src/model/Commands.cpp：撤销命令基于 QUndoCommand（属于
# QtWidgets），而本工程只用 QtCore。几何内核不依赖它，去掉它能让这个目标
# 成为"model 层的几何部分零界面依赖"的更强证据。
# 撤销栈的正确性由 06 阶段的手工清单在 GUI 里验证（见 docs/test.md）。

# 与主工程保持一致的编码设置：源文件是 UTF-8 无 BOM，MSVC 必须显式告知，
# 否则中文注释会被按 GBK 解码并抛出极具误导性的 C1075
msvc {
    QMAKE_CFLAGS   += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

INCLUDEPATH += $$PWD/../src

SOURCES += \
    GeometrySmoke.cpp \
    ../src/model/GeometryMath.cpp \
    ../src/model/Shape.cpp \
    ../src/model/GeoDocument.cpp \
    ../src/model/Camera.cpp

HEADERS += \
    ../src/model/GeometryMath.h \
    ../src/model/Shape.h \
    ../src/model/GeoDocument.h \
    ../src/model/Camera.h
