# ---------------------------------------------------------------------------
# GeoBoard — qmake 工程文件（应用程序）
#
# 说明：
#   * 本文件由 qt_scaffold.py 生成，也可作为手写参考；
#     对应的"带完整注释的模板"在 templates/qt/app.widgets.pro.txt。
#   * 界面全部用 C++ 代码构建，因此没有 FORMS 段，也不包含 uic 步骤。
#
# 维护纪律（见 workflow/stages/04_code/code_rule.md 第五节）：
#   每新增一个 .cpp 或 .h，**必须**同时加进下面的 SOURCES / HEADERS，
#   否则一旦被引用就是 LNK2019 无法解析的外部符号，而报错不会提示"你忘了改 .pro"。
#   改完 .pro 必须删除构建目录重新 qmake。
# ---------------------------------------------------------------------------

# svg 模块用于"导出为 SVG"（QSvgGenerator）。它同时是运行期依赖：
# 打包时 windeployqt 会带上 Qt5Svg.dll，已核实 dist/ 里有它。
QT += core gui widgets svg
CONFIG += c++11

TEMPLATE = app
TARGET   = GeoBoard

# ---------------------------------------------------------------------------
# 最低 Qt 版本守卫。
#
# 注意：qmake **没有** QT_VERSION_MIN 这个变量，写上去只会被静默忽略 ——
# 想真的要求某个版本，必须用 lessThan() 判一次。本 skill 以 Qt 5.12.12 为
# 基准开发；要放宽或抬高要求就改下面两个数字（并在项目 README 里说明）。
# ---------------------------------------------------------------------------
lessThan(QT_MAJOR_VERSION, 5)|lessThan(QT_MINOR_VERSION, 12) {
    error("本项目需要 Qt 5.12 或更高版本，当前为 Qt $$QT_VERSION")
}

# 注意：这里刻意**不写** CONFIG += object_parallel_to_source。
# 该选项会让 obj 落到 <构建目录>/release/src/ 这类子目录，而 qmake 生成的
# Makefile 不会自动创建这些子目录，nmake 会在链接阶段报
#   LINK : fatal error LNK1181: 无法打开输入文件"release\src\main.obj"
# 由于本工程约定"在独立构建目录里构建"，源码树本来就不会被污染，无需该选项。

CONFIG += warn_on
DEFINES += QT_DEPRECATED_WARNINGS

# ---------------------------------------------------------------------------
# 中文源码必须配套的编译选项（非常重要，漏掉会直接编译失败）
#
# 本项目所有源文件都以 **UTF-8 无 BOM** 保存。MSVC 默认按系统 ANSI 代码页
# （简体中文 Windows 上是 GBK/936）读取源文件，于是 UTF-8 的中文注释会被
# 误解码，常表现为莫名其妙的括号不匹配错误：
#     MainWindow.h(15): fatal error C1075: “{”: 未找到匹配令牌
# 以及满屏的
#     warning C4819: 该文件包含不能在当前代码页(936)中表示的字符
#
# 解决办法是显式告诉 MSVC 源文件是 UTF-8。MinGW (g++) 默认即为 UTF-8，
# 因此只在 MSVC 下追加该选项。
# ---------------------------------------------------------------------------
msvc {
    QMAKE_CFLAGS   += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

# ---------------------------------------------------------------------------
# 版本号与名称。
#
# 这里刻意**不写** DEFINES += APP_VERSION=\"x.y.z\"：该写法要经 qmake →
# 响应文件 → cl.exe 三层引号转义，实测在本机 MSVC 下会退化成裸的 x.y.z，
# 导致 QStringLiteral(APP_VERSION) 之类的用法直接编译失败，而报错指向
# QString 内部，极难定位。版本号因此定义在 src/version.h 里。
# ---------------------------------------------------------------------------
DEFINES += APP_NAME=\"GeoBoard\"

INCLUDEPATH += $$PWD/src

SOURCES += \
    src/main.cpp \
    src/model/Camera.cpp \
    src/model/Commands.cpp \
    src/model/GeoDocument.cpp \
    src/model/GeometryMath.cpp \
    src/model/Shape.cpp \
    src/service/JsonStore.cpp \
    src/controller/MainController.cpp \
    src/controller/ToolId.cpp \
    src/view/CanvasWidget.cpp \
    src/view/GeoBoardMainWindow.cpp \
    src/view/SceneExporter.cpp \
    src/view/ShapeListPanel.cpp

HEADERS += \
    src/version.h \
    src/model/Camera.h \
    src/model/Commands.h \
    src/model/GeoDocument.h \
    src/model/GeometryMath.h \
    src/model/Shape.h \
    src/service/JsonStore.h \
    src/controller/MainController.h \
    src/controller/ToolId.h \
    src/view/CanvasWidget.h \
    src/view/GeoBoardMainWindow.h \
    src/view/SceneExporter.h \
    src/view/ShapeListPanel.h
# ---------------------------------------------------------------------------
# 几何引擎自测与性能基准（06 阶段）**不在本工程内**。
#
# 原因（实测踩过）：同一个 .pro 里的两个 app 目标会把 obj 都写进同一个
# release/ 目录，于是两边的 main.cpp 撞成重定义：
#     geometry_smoke.obj : error LNK2005: main 已经在 main.obj 中定义
#     fatal error LNK1169: 找到一个或多个多重定义的符号
# 因此它是独立工程 tests/GeometrySmoke.pro，拥有自己的构建目录。
#
# 构建与运行：scripts\build.bat test（详见 docs/test.md）
# ---------------------------------------------------------------------------

# 资源文件。默认只有一个空前缀，用到图标/QSS 时把文件加进 resources/app.qrc，
# 或在 Qt Creator 里编辑（注意：本 skill 禁止 .ui，但 .qrc 是正常的）。
RESOURCES += resources/app.qrc
