# GeoBoard 代码审查记录

> **更正块（2026-09-27，审查之后追加）**：本文件成文时唯一遗留的需求缺口 **F42
> 「状态栏或界面显示当前选中对象的数量」现已实现**，"通过 34 / 部分通过 1 / 不通过 6"
> 的统计随之变为 **通过 35 / 部分通过 0 / 不通过 6**。实现与证据：
>
> | 项 | 内容 |
> |---|---|
> | 新增接口 | `GeoBoardMainWindow::setSelectionCount(int)`——`src/view/GeoBoardMainWindow.h:119`（声明）、`src/view/GeoBoardMainWindow.cpp:413`（实现：0 显示"未选中"，否则显示"已选中 N 个"） |
> | 状态栏控件 | `m_selectionLabel`，与鼠标坐标、缩放百分比并列放在状态栏右侧，并带 `setToolTip` 说明用法 |
> | 调用点 | `MainController::onSelectionChanged()`（`src/controller/MainController.cpp:895`）——以**文档当前的选中集**为准而不是信号参数，避免删除后瞬时不一致；`start()` 的 `:184` 与 `resetForNewDocument()` 的 `:1058` 各复位为 0 |
> | 验证 | `scripts\build.bat build` 通过；`scripts\build.bat test` 48 项 0 失败；`scripts\build.bat json` 19 项 0 失败；`qt_verify.py` 结果 OK（OK=15 INFO=0 WARN=0 FAIL=0） |
> | **未取得的部分** | 状态栏的**截图证据没有取到**：本机为 200% 缩放，`GetWindowRect` 返回的窗口高度与实绘物理高度不一致，多次抓取都只覆盖到画布区域。因此该功能的验证是"编译通过 + 调用路径核对 + 与既有的 setZoomPercent 同构"，而**不是**截图目视确认——这一项如实标注为**待人工目视复核** |
>
> 下文第 3 节"残留说明"与第 7 节判据表中关于 F42 的表述以本更正块为准。

日期：2026-09-27
审查范围：`GeoBoard.pro`、`tests/GeometrySmoke.pro`、`src/`（model / view / controller / service，共 20 个源文件）、
`tests/GeometrySmoke.cpp`、`scripts/build.bat`、`docs/`（需求、设计、命名）；
修复后复核另含 `tests/JsonSmoke.{cpp,pro}`（本轮新增的文件往返自测工程）。
规范与 41 条人工审查清单见 `workflow/stages/05_verify/verification.md`。

审查方式：先跑 `qt_verify.py`，再做人工静态审查（逐文件阅读 + `grep` 定位到行）。
**界面交互（点击序列、拖动、撤销重做的实际操作）不在本次审查范围内**，由 06 阶段的人工清单负责
（见 `docs/test.md` 第 6 节）；凡属运行时才能判定的项，本记录一律标注为"未运行时验证"。

**修复后复核（R1 / R2 / S10 关闭后重跑）**：`src/view/MainWindow.{h,cpp}` 已删除；工具栏测量组新增
"圆周长 / 圆面积 / 点坐标"三个工具并拆成**两行**（作图工具 14 个 / 测量工具 8 个，共 22 个），
`MeasurementShape::recompute()` 的 `Area` 分支已同时支持多边形与圆，`JsonStore` 加载时 `Area` 的关联
图元也接受圆（修掉"圆面积标注存得下、打不开"的往返缺陷）；`GeoBoard.pro` 中失效的
`include(tests/geometry_test.pri)` 已删除；测试侧新增文件往返自测工程 `tests/JsonSmoke.{cpp,pro}`。
本文档第 1、2、3、4、6、7 节已按修复后的真实状态重写，凡结论变化处均标注"（修复后复核）"。

## 1. 脚本检查结果

命令：

```bat
python scripts\qt_verify.py --project C:\Users\a\Desktop\functino\GeoBoard
```

本次修复后复核运行输出：`结果: OK  (OK=15 INFO=0 WARN=0 FAIL=0)`，退出码 `0`（无 WARN、无 FAIL）。

| # | 检查项 | 结果 | 说明 |
|---|---|---|---|
| 1 | 项目目录 | OK | `C:\Users\a\Desktop\functino\GeoBoard` |
| 2 | 必备目录 | OK | `src/` |
| 3 | 建议目录 | OK | 全部就位 |
| 4 | 顶层 `.pro` | OK | `GeoBoard.pro`、`tests\GeometrySmoke.pro`、`tests\JsonSmoke.pro` |
| 5 | `.gitignore` | OK | 已存在 |
| 6 | 必备文档 | OK | `docs/design.md` |
| 7 | 建议文档 | OK | 全部就位 |
| 8 | `.pro` 引用完整性 | OK | 检查 **3 个 `.pro`、41 条引用，全部存在** |
| 9 | 纯 C++ 界面约束 | OK | **未发现 `.ui` 引用，也没有 `.ui` 文件** |
| 10 | 文件命名（大驼峰） | **OK（已消除历史 WARN）** | 曾报 `tests/geometry_smoke.cpp`；已改名为 `tests/GeometrySmoke.cpp`，并同步 `tests/GeometrySmoke.pro` 与 `scripts/build.bat` 的引用，复核运行输出"全部符合"。新增的 `tests/JsonSmoke.cpp` 同样符合大驼峰 |
| 11 | Doxygen 文件头 | OK | 检查 **24 个文件，全部具备**（源文件数 25 → 23 → 24：先减去被删除的脚手架残留 `MainWindow.{h,cpp}` 两个文件，再加上新增的 `tests/JsonSmoke.cpp`） |
| 12 | 废弃写法与 Qt5 信号槽风格 | OK | **未发现**（无 `SIGNAL()/SLOT()`、`QRegExp`、`qSort`、`Q_FOREACH`、`#include <QtWidgets>`） |
| 13 | 构建冒烟测试 | OK | 状态 OK（`OK=11 WARN=0 FAIL=0`） |
| 14 | 构建垃圾残留 | **OK（修复后复核已消除）** | "工作区干净"：交付产物已移出项目目录，项目内不再有打包目录 → S11 消除 |
| 15 | 交付结论 | **OK（修复后复核已消除）** | "机械校验全部通过" |
| 16 | FAIL 项 | **0** | 无 FAIL，可进入 06 阶段 |

**曾报的两条 WARN，现已全部消除（修复后复核）。**

* 第 10 项（文件命名）是**已消除**的历史 WARN：它曾指向 `tests/geometry_smoke.cpp` 违反大驼峰约定。
  改名后复核运行输出为"全部符合"，且三处引用（文件名、`.pro` 的 `SOURCES`、构建脚本）已逐一核对一致。
* 第 14/15 项是**打包目录命名**引起的提示，不涉及源码与构建正确性：交付产物（原 `GeoBoard-0.1.0-win64\`）
  已从项目目录移出，放到 `C:\Users\a\Desktop\functino\` 下（与 `dist/` 同性质），因此本次复核
  输出"工作区干净"与"机械校验全部通过" → **S11 消除**，不阻塞 06。

**脚本未覆盖、曾靠人工发现的一处（已修复）。** `qt_verify.py` 的"`.pro` 引用完整性"只校验
`SOURCES / HEADERS / RESOURCES` 里列出的文件是否存在，**不检查 `include()` 的目标**。上一轮复核实际执行
`qmake GeoBoard.pro`（在临时构建目录中，不改工程）曾得到：

```
Cannot read C:/Users/a/Desktop/functino/GeoBoard/tests/geometry_test.pri: No such file or directory
```

即 `GeoBoard.pro` 里的 `include(tests/geometry_test.pri)` 指向的文件已在"测试拆为独立工程"时被删除，
每次 qmake 都会报一次找不到文件（退出码仍为 0，构建不受影响）→ 记入 **S10**。
**修复后复核**：该 `include` 行已删除，原位置（现 `GeoBoard.pro:97-107`）改为说明"几何引擎自测不在本
工程内、它是独立工程 `tests/GeometrySmoke.pro`、构建与运行见 `scripts\build.bat test`"的注释；
`scripts\build.bat build` 实测 qmake 不再输出 `Cannot read …` 警告。

## 2. 人工审查结果（41 条逐条结论）

| # | 检查项 | 结论 | 位置 | 备注 |
|---|---|---|---|---|
| 1 | Model 是否引用界面头文件 | 通过 | `src/model/`（`Camera/Commands/GeoDocument/GeometryMath/Shape` 及实现） | 全目录搜索 `QWidget/QMainWindow/QMessageBox/QDialog/QtWidgets/ui_` **无匹配**。唯一跨界是 `src/model/Commands.h:22` 的 `#include <QUndoCommand>`（QtWidgets 模块的类），取舍见 S8 |
| 2 | View 是否含业务规则 | 通过 | `src/view/CanvasWidget.cpp`、`GeoBoardMainWindow.cpp` | View 内没有作图/文件/校验规则；唯一"跨界"是画布直接维护文档的**选中集**（`CanvasWidget.cpp:760`、`780-781`），这是设计文档明确记录的例外（`CanvasWidget.h:8-9`：选中集属于界面状态）。工具语义判断在控制器：`MainController.cpp:828-846` 明确拒绝拖动中点/交点 |
| 3 | View 是否直接做 IO | 通过 | `src/view/` | 只有 `QFileDialog`（`GeoBoardMainWindow.cpp:420`、`429`）与 `QFileInfo`（`:412`，纯路径字符串处理）；**无** `QFile/QDir/QSqlDatabase/QNetworkAccessManager` 的直接读写（全项目搜索确认） |
| 4 | Controller 是否直接操作 View 内部控件 | 通过 | `src/controller/MainController.cpp` | 全文 **121 处** `m_view->` 全部是公开方法：`canvas() / setStatusMessage / setPreview / setActiveTool / setMouseStatus / setModified / setUndoRedoEnabled / setZoomPercent / setFileName / promptOpenPath / promptSavePath / showError / askSaveChanges / showAbout`；**无** `m_view->m_xxx` |
| 5 | Service 是否依赖 View | 通过 | `src/service/JsonStore.h:31-37` | 只 include `model/GeoDocument.h` 与 QtCore（`QHash/QJsonObject/QList/QObject/QString`）；`JsonStore.cpp` 只 include `QFile/QSaveFile/QJson*` |
| 6 | View 与 Controller 是否只通过信号槽 | 通过 | `src/view/`、`src/main.cpp:27-28` | `src/view/` 内**没有任何** `MainController` 引用，View 不持有控制器指针；两者只在 `main()` 里由栈对象组装 |
| 7 | 契约表每一行都有对应 `connect` | **不通过** | `docs/design.md` 4.3 第 2 行 vs `src/model/GeoDocument.h:256-261` | 4.1（11 行）与 4.2（11 行）逐行都能在 `MainController.cpp:69-79`、`:82-95` 找到对应 `connect`；但 4.3 的 `GeoDocument::modifiedChanged(bool) → onModifiedChanged` **在代码中不存在**：`GeoDocument` 只声明 `documentChanged` 与 `selectionChanged`，未保存标记改由 `QUndoStack::cleanChanged` 驱动（`MainController.cpp:104-105` → `GeoBoardMainWindow::setModified`，`GeoBoardMainWindow.cpp:383-386`）→ 见 S3 |
| 8 | 代码里每个 `connect` 在契约表里有记录 | **不通过** | `GeoBoardMainWindow.h:182`、`MainController.cpp:94`、`GeoBoardMainWindow.h:82`、`:100` | 未登记的连线与命名差异：① `fitContentRequested()` 不在 4.2 表内；② View 内部三组连线（`m_toolGroup → onToolActionTriggered`、`deleteAction → CanvasWidget::deleteRequested`、`m_undoStack` 三个状态信号 → 界面）不在任何表内；③ 4.4 表写 `showStatusMessage(...)`，代码实际是 `setStatusMessage(...)`；④ `setFileName(...)` 未登记 → 见 S3 |
| 9 | 所有 `connect` 的返回值都被检查 | **不通过** | 仅 2 处：`CanvasWidget.cpp:171-174`、`GeoBoardMainWindow.cpp:324-327` | `MainController.cpp:69-105` 共 **28 处** `connect` 均未检查返回值（Qt5 编译期语法下签名不匹配会直接编译失败，实际不会返回 false，但按清单要求仍属"未检查"）→ 见 S4 |
| 10 | 没有使用 `SIGNAL()/SLOT()` | 通过（修复后复核） | 全项目搜索 | 无匹配。上一轮唯一出现处是 `src/view/MainWindow.cpp:27` 的注释（"禁止使用 Qt4 的 `SIGNAL()/SLOT()` 宏"）；该脚手架残留文件删除后，全项目已无任何 `SIGNAL()/SLOT()` 字样。所有连线都是 Qt5 编译期语法，无字符串拼接写法 |
| 11 | lambda 形式的 `connect` 都传了上下文对象 | 通过 | `GeoBoardMainWindow.cpp:148-230`、`MainController.cpp:97-105`、`:166` | 全部写成 `connect(发送方, 信号, this, [this](...){...})`，第 3 个参数均为 `this` |
| 12 | 有无 `delete` 有父对象的 `QObject` | 通过 | `GeoDocument.cpp:433`、`JsonStore.cpp:564-566`、`Commands.cpp:34`、`:85` | 被 `delete` 的全是 `Shape`（**不是** `QObject`，由 `GeoDocument` 拥有，`GeoDocument.cpp:280`/`:516` 统一 `qDeleteAll`）与命令持有的快照；未发现删除有父对象 `QObject` 的代码 |
| 13 | 有无 `new` 之后无父对象又无智能指针 | 通过 | `GeoBoardMainWindow.cpp:97-292`、`MainController.cpp:49-51` | 所有 `QObject` 派生对象都带父对象（控件/动作 `this`，`GeoDocument/JsonStore/QUndoStack` 父为控制器）；局部 `new QVBoxLayout`（`GeoBoardMainWindow.cpp:100`）随即由 `central->setLayout()` 接管（`:105`）；`Shape` 由 `addShape()` 移交文档所有权（`GeoDocument.h:115-121`） |
| 14 | 跨线程对象是否用信号槽通信 | 通过（不适用） | 全项目搜索 `QThread/QtConcurrent` | 无匹配：本项目单线程，未创建任何工作线程 |
| 15 | `QObject` 成员在构造时是否初始化 | 通过 | `GeoBoardMainWindow.cpp:51-74`、`CanvasWidget.cpp:122-141`、`MainController.cpp:46-64` | 三个类的初始化列表逐项覆盖了头文件里的全部成员（含全部 `QAction*` 与状态机成员）；`Camera`/`Vec2` 等值成员有默认构造 |
| 16 | 可能失败的操作是否检查了返回值 | 通过 | `JsonStore.cpp:375-391`、`:424-445`、`MainController.cpp:1040-1043`、`:1063-1066`、`:945` | `open/write/commit/readAll/error` 逐个检查并设置 `lastError`；`load/save/fitContent` 的返回值都在控制器里判断。唯一未检查处：`MainController.cpp:859` 的 `movePoint()`（该分支下 id 已在 `:828-846` 校验、坐标来自 `QPointF`，不可失败），并入 S4 |
| 17 | 错误信息是否说明"原因 + 怎么办" | 通过 | `JsonStore.cpp:395-403`、`:425-431`、`:438-443`、`:452-460`、`:474-478`、`:489-499`、`:507-511`、`:523-529`、`:537-544`、`:570-573` | 每条 `m_lastError` 都含"可能原因 + 建议动作"，例如"文件被其他程序占用（请关闭后重试）/目标目录没有写入权限（请另存到桌面）"；版本过高时提示"请升级 GeoBoard 后再打开" |
| 18 | 有无静默忽略的失败（空分支） | 通过 | 全项目搜索 `\)\s*\{\s*\}` | 只命中内联空构造函数（`Shape.h:150`、`:163`、`GeometryMath.h:29/36/42`），无"`if (!ok) { }`"式空分支 |
| 19 | 有无用 `qDebug()` 代替用户提示 | 通过 | `src/` 全目录搜索 `qDebug/qWarning` | 无任何调用；仅有 `JsonStore.cpp:22`、`JsonStore.h:42` 的"不写 qDebug 日志"声明 |
| 20 | 需求「必须实现的功能」是否逐条实现 | **部分通过（修复后复核）** | `src/controller/ToolId.h:16-40`、`MainController.cpp:687-766`、`Shape.cpp:727-777`、`Shape.h:71`、`JsonStore.cpp:1350-1383`、`GeoBoardMainWindow.cpp:296-321` | F1–F43 中除 F42 外均有实现路径。**F20（圆周长）、F21（圆面积）、F23（点坐标标注）已修复**：工具栏由 19 个工具增至 **22 个**并拆成**两行**（第一行"作图工具"14 个 = 选择 1 + 基础作图 8 + 派生约束 5，第二行"测量工具"8 个；`GeoBoardMainWindow.cpp:270-321`，两行分隔由 `:285` 的 `addToolBarBreak()` 实现），测量组新增"圆周长"（`ToolId::MeasureCircumference`，`GeoBoardMainWindow.cpp:318`）、"圆面积"（`ToolId::MeasureCircleArea`，`:319`）、"点坐标"（`ToolId::MeasureCoordinates`，`:321`）三个工具；`MainController::handleMeasurement` 新增这三条创建路径（圆周长/圆面积 `:687-741`、点坐标 `:743-766`），`refreshInputHighlight` 对"点坐标"改为高亮点；`MeasurementShape::recompute()` 的 `Area` 分支**同时支持多边形（鞋带公式）与圆（πr²）**（`Shape.cpp:743-777`），`MeasurementKind::Area` 的注释已更新为"面积（多边形按鞋带公式，圆按 πr²）"（`Shape.h:71`）；文件格式同步放开了 `Area` 的关联图元类型（`JsonStore.cpp:1350-1383`）——它曾把 `Area` 的 target 写死为 `Polygon`，使圆面积标注"存得下、打不开"，本轮一并修复（见第 6 节 D9）；`GeoBoardMainWindow.cpp:466` 的"关于"对话框声称支持"周长"测量，现在与实现一致。**仍缺一处子项**：F42 要求"状态栏或界面显示当前选中对象的数量"，而 `MainController::onSelectionChanged()`（`MainController.cpp:888-892`）仍是 `Q_UNUSED(ids)`、只更新撤销/重做的可用性，画布也只做高亮（`CanvasWidget.cpp:418-430`），全项目没有任何显示选中数量的代码 → R2 的 F20/F21/F23 部分**已关闭**，F42 子项仍在（见第 3 节"残留说明"） |
| 21 | 「明确不做」的功能是否确实没做 | 通过 | 全项目搜索 `QSettings/QPrinter/QSvgGenerator/QPixmap/QImage/CMakeLists/.ts/.qm` | 无匹配：无导出图片、无打印、无 QML、无 CMake、无翻译文件、无 `QSettings`（窗口记忆按决策不做，见 design.md 决策表） |
| 22 | 每条功能的验收方式是否可验证 | 通过 | `docs/requirements.md` 第 3 节 | 43 条每行都给出了可判定的验收方式（含容差与判定口径），能直接改写成测试步骤；F20/F21/F23 的验收文本本身可执行，**修复后复核**：三者已具备界面入口（同第 20 项），验收文本现在可以照做 |
| 23 | 函数是否过长（> 60 行） | **不通过** | `MainController.cpp:633-766`、`:305-371`、`:373-434`、`:111-172` | **4 个**函数超阈值（修复后复核重测）：`handleMeasurement` **134 行**（远超阈值，内含"两点距离 / 三点角度 / 单选图元 / 点坐标"四套流程，新增三个测量工具后由 103 行增长）；`onCanvasClicked` **67 行**（纯分发 switch，每个 case 3–5 行，职责单一但确实超行数）；`handleTwoPointTool` **62 行**、`createDemoConstruction` **62 行**（"取第一点"与"取第二点"两段 / 演示图形装配）→ 见 S1 |
| 24 | 重复代码块（3 次以上应抽取） | **不通过** | `MainController.cpp:317`、`:382`、`:465`、`:484`、`:586`、`:682` | `if (!created.isEmpty()) { m_undoStack->push(new AddShapesCommand(m_document, created, tr("新建点"))); }` 这一组样板出现 **6 次**（修复后复核：新增工具后由 4 次变为 6 次）；"结束作图回到选择工具"两行（`cancelPendingDrawing(); onToolChangeRequested(ToolId::Select);`）出现 **6 次**（`:513-514`、`:552-553`、`:580-581`、`:629-630`、`:662-663`、`:676-677`），另有 4 处单独出现的 `onToolChangeRequested(ToolId::Select)`（`:428`、`:460`、`:740`、`:760`）→ 见 S2 |
| 25 | 命名是否符合 `naming.md` | 通过 | `src/` 全目录 | 类/结构/枚举大驼峰（`GeoDocument`、`LineData`、`ToolId`）、成员 `m_` 前缀、常量 `k` 前缀（`kPi`、`kPositionTolerance`）、槽 `onXxx`、信号 `xxxRequested/xxxChanged`、取值函数无 `get`；未发现匈牙利前缀或自造缩写。注意：`docs/naming.md` 自身第 29/35/39 行仍是脚手架遗留的空表格单元（写着"待填写"之类的话）→ 见 S5 |
| 26 | 头文件是否包含多余的头文件 | 通过 | `GeoBoardMainWindow.h:22-27`、`MainController.h:27-28`、`GeoDocument.h:21-28` | 该前置声明的都用了前置声明（`CanvasWidget/QAction/QLabel/QMenu/QToolBar`、`GeoBoardMainWindow/QUndoStack`）；仅 `GeoDocument.h:26` 的 `#include <QPointF>` 在本头文件内未被使用（方法参数一律用 `Vec2`）→ 可选清理，见 S6 |
| 27 | 有无遗留调试代码、注释掉的死代码 | **通过（修复后复核）** | `src/view/`（原 `MainWindow.h` 59 行、`MainWindow.cpp` 65 行，**已删除**） | 这两个文件是脚手架残留，**现已删除**：`src/view/` 只剩 `CanvasWidget.{h,cpp}` 与 `GeoBoardMainWindow.{h,cpp}`，类重定义与包含保护宏冲突的风险随之消失。原问题：它们**不在** `GeoBoard.pro` 的 `SOURCES/HEADERS` 中，却定义了与正式实现**同名的类 `GeoBoardMainWindow`**，且与正式头文件共用**同一个包含保护宏 `GEOBOARDMAINWINDOW_H`**；内容仍是模板示例（`QLabel("Hello, Qt5 Widgets.")`、空 `createActions()`）→ **R1 已关闭**（验证见第 3 节） |
| 28 | 有无裸 `TODO` | 通过 | `src/` 全目录搜索 `TODO/FIXME/XXX/待实现` | 无匹配（"暂时"仅出现在 `MainController.cpp:612`、`Shape.cpp:614` 的用户可见文案里，不是待办标记） |
| 29 | 注释是否与代码一致 | **不通过** | `CanvasWidget.cpp:444`、`tests/GeometrySmoke.cpp:2` | ① `CanvasWidget.cpp:444` 注释写派生点用"**空心**/不同色"的观感，但 `drawPointMarker()`（`:507-530`）对自由点与派生点都用 `setBrush(fill)` 实心填充，二者**只有颜色不同**；② `tests/GeometrySmoke.cpp:2` 的文件头仍写 `@file tests/geometry_smoke.cpp`，而文件已改名为 `GeometrySmoke.cpp` → 见 S7 |
| 30 | 无 `.ui` 文件、界面全为 C++ | 通过 | 脚本 OK 项 + `src/` 目录 | 无 `.ui` 文件、`.pro` 无 `FORMS`、无 `ui_*.h` 引用；界面全部在 `GeoBoardMainWindow.cpp`（**470 行**，修复后复核：新增三个测量工具动作后为 457 行，工具栏拆成两行后为 470 行）里用布局装配 |
| 31 | 无 `QApplication::setStyle("Fusion")` | 通过 | 全项目搜索 `setStyle/QStyleFactory/Fusion` | 无匹配（仅 `docs/` 文字说明"不设置 Fusion"） |
| 32 | 未在全局 `QWidget` 上刷背景色 | 通过 | `CanvasWidget.cpp:144`、`:271` | 无样式表（`setStyleSheet` 无匹配）；画布只对**自己**关闭自动填充并在 `paintEvent` 用 `palette().color(QPalette::Base)` 填充自身矩形，其余控件沿用系统样式；所有颜色取自 `palette()`，无硬编码色值 |
| 33 | 交互控件可用 Tab 到达且有可见焦点 | 通过（静态；运行时未验证） | `GeoBoardMainWindow.cpp:270-321`、`CanvasWidget.cpp:142` | 所有工具入口都是标准 `QToolBar + QAction`（文字按钮）与菜单项，无自绘控件；画布 `setFocusPolicy(Qt::StrongFocus)` 可接收键盘事件。**修复后复核**：工具栏已拆成两行（作图工具 14 个 / 测量工具 8 个，`addToolBarBreak()` 在 `:285`），因此 22 个工具在常见窗口宽度下**恒定可见、可点击**——此前单行排不下时，`setMovable(false)` 的 `QToolBar` 不显示溢出按钮，测量工具会被挤出可见区而无法点击（见第 6 节 D12）。**Tab 顺序与焦点可见性仍属运行时行为，未走查**，已列入 `docs/test.md` 第 6 节 |
| 34 | `QLabel` 关联的输入框设置 `setBuddy()` | 通过（不适用） | `GeoBoardMainWindow.cpp:114-124` | 界面没有输入框，两个 `QLabel` 只是状态栏的坐标/缩放显示（`addPermanentWidget`），不存在需要 `setBuddy()` 的标注-输入对 |
| 35 | 不只靠颜色传达信息 | 通过 | `CanvasWidget.cpp:422-430`、`:460-462`、`:515-522` | 选中/高亮同时改变**颜色**（`QPalette::Highlight`）、**线宽**（1.6 → 2.4）与**标记尺寸**（额外光晕圆）；派生线用**虚线**与直接绘制的线区分。唯一仅靠颜色区分的是"自由点 vs 派生点"（`:524-525` 取 `Link` 与 `WindowText`），可选改进，见 S9-备注 |
| 36 | 长操作在子线程执行、UI 不阻塞 | 通过（不适用） | 全项目：无 `QThread`；`JsonStore` 同步读写 | 没有长耗时操作，文件读写为同步调用，量级为几百 KB 文本。**1000 图元文件的打开耗时未实测**（需求 F32 的 3 秒判定），已列入 `docs/test.md` 待执行项 |
| 37 | 每个新增的 `.cpp/.h` 都在 `.pro` 里 | **通过（修复后复核）** | `GeoBoard.pro:71-95` | 反向核查：`SOURCES`（11 条）+ `HEADERS`（11 条）= **22 条**，与磁盘上 `src/` 的 22 个文件（含 `main.cpp`、`version.h`）逐一对应，既无遗漏也无多余——脚本的"引用全部存在"只验证正向，反向靠人工核对。原先多出的 `src/view/MainWindow.h`、`src/view/MainWindow.cpp` 已删除，与第 27 项同因 → **R1 已关闭** |
| 38 | `.pro` 里有 `msvc { QMAKE_*FLAGS += /utf-8 }` | 通过（修复后复核：新增的 `tests/JsonSmoke.pro` 同样具备） | `GeoBoard.pro:54-57`、`tests/GeometrySmoke.pro:38-41`、`tests/JsonSmoke.pro:30-33` | 三个 `.pro` 都只在 `msvc` 分支下追加 `QMAKE_CFLAGS/QMAKE_CXXFLAGS += /utf-8`，注释说明了缺它会触发 C1075/C4819 的原因 |
| 39 | `.pro` 里没有 `object_parallel_to_source` | 通过 | `GeoBoard.pro:32-36` | 全文无该选项，注释解释了它会导致 `LNK1181` 的原因 |
| 40 | 删除构建目录后能从零构建成功 | 通过（依据既有构建记录；本次审查未重新执行） | `scripts/build.bat:57-70`（构建）、`:72-88`（test）、`:90-106`（json） | 脚本对 build-release / build-test / build-json 都是"目录不存在则新建 → qmake → nmake"，三个构建目录均已由该脚本产出 `GeoBoard.exe`、`geometry_smoke.exe` 与 `json_smoke.exe`；`docs/delivery.md` 第 6 节记录了从零构建通过。**本次审查没有删除构建目录重跑**，故此项以既有记录为据 |
| 41 | `.gitignore` 覆盖 `build*/`、`*.pro.user`、`moc_*`、`.qt-skill/` | 通过 | `.gitignore:13`、`:49-50`、`:41-42`、`:63` | 四项全覆盖，并额外含 `dist/`、`GeoBoard-*-win64/`、`*.zip`、`qtskill.json`、`__pycache__/` |

**统计（修复后复核）：41 条中通过 34 条、部分通过 1 条、不通过 6 条。**
不通过：第 7、8、9、23、24、29 项；部分通过：第 20 项（F20/F21/F23 已修复，F42 子项仍未实现）。
因本轮修复由"不通过"转为"通过"的是第 27、37 项（R1，脚手架残留文件已删除）。
不通过项按"是否阻塞 06"分别进入第 3 节与第 4 节。

## 3. 必须修复（阻塞 06 阶段）

| 编号 | 问题 | 位置 | 修法（含修复后复核的验证方式） |
|---|---|---|---|
| **R1 ✅ 已修复** | 脚手架残留文件：定义了与正式实现同名的 `GeoBoardMainWindow` 类，且与正式头文件共用同一个包含保护宏 `GEOBOARDMAINWINDOW_H`；不在 `.pro` 中（审查项 27、37 不通过） | 原 `src/view/MainWindow.h:14-17`、`src/view/MainWindow.cpp:17`（**两个文件已删除**） | **修法：删除这两个文件**（内容已是模板示例，无任何被引用的符号）。复核：`src/view/` 现在只有 `CanvasWidget.{h,cpp}` 与 `GeoBoardMainWindow.{h,cpp}`；`scripts\build.bat build` 与 `scripts\build.bat test` 均通过；`qt_verify.py` 的 Doxygen 文件头检查数由 25 降到 23，正好对应删除的 2 个文件（另加新增的 `tests/JsonSmoke.cpp` 后为 24）。类重定义 / 包含保护宏冲突的风险随之消失 |
| **R2 ✅ 已修复（F20 / F21 / F23）** | 需求 F20（圆周长）、F21（圆面积）、F23（点坐标标注）**没有界面入口**，其中圆面积连模型都未实现（审查项 20 不通过） | `ToolId.h:16-40`、`MainController.cpp:687-766`、`Shape.cpp:727-777`、`JsonStore.cpp:1350-1383`、`GeoBoardMainWindow.cpp:296-321` | **修法：采用"补齐实现"方案。** ① `ToolId` 新增 `MeasureCircleArea` / `MeasureCircumference` / `MeasureCoordinates`，工具栏由 19 个工具变为 **22 个**（选择 1 + 基础作图 8 + 派生约束 5 + 测量 8）；② `MainController::handleMeasurement` 新增这三条创建路径，`refreshInputHighlight` 对"点坐标"改为高亮点；③ `MeasurementShape::recompute()` 的 `Area` 分支同时支持多边形（鞋带公式）与圆（πr²）；④ 文件格式跟上——`JsonStore` 加载时 `Area` 的关联图元接受 `Polygon` 或 `Circle`（原先写死 `Polygon`，会让圆面积标注"存得下、打不开"，见第 6 节 D9）。复核：`scripts\build.bat build` 通过；`scripts\build.bat test` 通过，"派生图元"用例集由 9 项增至 **14 项**、总断言 **48 项 0 失败**；`scripts\build.bat json` 通过，往返自测 **19 项 0 失败**（其中"圆面积标注必须能读回"是该缺陷的回归点）。**遗留子项**：F42 的"状态栏或界面显示当前选中对象的数量"仍未实现（见下方"残留说明"） |
| **S10 ✅ 已修复** | 主 `.pro` 仍 `include(tests/geometry_test.pri)`，而该文件已随"测试拆为独立工程"被删除；实测每次 qmake 报 `Cannot read …: No such file or directory`（退出码 0，不影响构建） | 原 `GeoBoard.pro:107`（及上方 `:104-106` 的说明注释） | **修法：删除该 `include` 行及对应注释**，原位置（现 `GeoBoard.pro:97-107`）改为一段说明"几何引擎自测不在本工程内、它是独立工程 `tests/GeometrySmoke.pro`、构建与运行见 `scripts\build.bat test`"的注释。复核：`scripts\build.bat build` 实测 qmake 不再输出 `Cannot read …`；`qt_verify.py` 的 `.pro` 引用完整性为 **3 个 `.pro`、41 条引用全部存在** |

**状态说明（修复后复核）**：R1、R2（F20/F21/F23 部分）、S10 **均已关闭**，
`verification.md` 第五节的完成判据"所有『必须修复』项已修复"**现已成立**（另见第 7 节）。
复核方式与结果：`scripts\build.bat build` 通过；`scripts\build.bat test` 输出
`结论：全部用例通过`（48 项断言 0 失败，退出码 0）；`scripts\build.bat json` 输出
`结论：全部用例通过`（19 项断言 0 失败，退出码 0）；
`python qt_verify.py --project .` 输出 `结果: OK  (OK=15 INFO=0 WARN=0 FAIL=0)`，退出码 0。

**残留说明（如实记录）**：R2 中与 F42"状态栏或界面显示当前选中对象的数量"相关的子项**仍未实现**——
`MainController::onSelectionChanged()`（`MainController.cpp:888-892`）只更新撤销/重做可用性，
`ids` 参数被 `Q_UNUSED` 忽略，全项目没有任何显示选中数量的代码。该项**不阻塞 06**（选中状态有可见高亮，
`CanvasWidget.cpp:418-430`），但需用户裁决"补实现"还是把 F42 的验收口径改为"界面用高亮表示选中、不显示数量"。

## 4. 建议修复（不阻塞）

| 编号 | 问题 | 位置 | 建议 |
|---|---|---|---|
| S1 | **4 个**函数超过 60 行阈值（**134 / 67 / 62 / 62 行**；修复后复核重测） | `MainController.cpp:633-766`（`handleMeasurement`）、`:305-371`（`onCanvasClicked`）、`:373-434`（`handleTwoPointTool`）、`:111-172`（`createDemoConstruction`） | 把 `handleMeasurement` 按测量族拆成私有函数（两点 / 三点 / 单选图元 / 点坐标，新增三个测量工具后由 103 行增至 134 行）；`handleTwoPointTool` 的"取第二个点并建图元"可独立成函数。不阻塞 06：这些函数是线性步骤序列，且不重入 |
| S2 | **6 处**"新建点入栈"样板与 **6 处**"结束作图回选择工具"样板（修复后复核重数，原为 4 处与 3 处） | `MainController.cpp:317`、`:382`、`:465`、`:484`、`:586`、`:682`、`:513-514`、`:552-553`、`:580-581`、`:629-630`、`:662-663`、`:676-677` | 抽取 `pushCreatedPoints(QList<Shape *> &created)` 与 `finishToolInteraction()` 两个私有函数；另有 4 处单独出现的 `onToolChangeRequested(ToolId::Select)`（`:428`、`:460`、`:740`、`:760`）可一并纳入 |
| S3 | `docs/design.md` 与代码不同步：4.3 的 `modifiedChanged` 行在代码中不存在；4.2 缺 `fitContentRequested`；4.4 的 `showStatusMessage` 实为 `setStatusMessage`，且缺 `setFileName` | `docs/design.md` 4.2/4.3/4.4 表 | 按代码更新契约表（保留 `QUndoStack::cleanChanged` 这一实际实现并说明理由），使"文档是唯一事实来源"重新成立 |
| S4 | **28 处** `connect` 未检查返回值；另 `MainController.cpp:859` 的 `movePoint()` 返回值未检查（修复后复核重数，原为 26 处与 `:816`） | `MainController.cpp:69-105`、`:859` | 统一改为 `const bool connected = connect(...); Q_ASSERT(connected); Q_UNUSED(connected);`，与 `CanvasWidget.cpp:171-174`、`GeoBoardMainWindow.cpp:324-327` 的做法一致 |
| S5 | 交付文档仍有脚手架占位符 | `docs/naming.md:29`、`:35`、`:39` | 术语表填上本项目的实际术语（点/曲线/派生图元/退化/附着点…），缩写写"无"，差异写"无" |
| S6 | `GeoDocument.h:26` 的 `#include <QPointF>` 在本头文件内未被使用 | `src/model/GeoDocument.h:26` | 删除该 include（若有使用者依赖它间接获得 `QPointF`，应在自己的文件里显式包含） |
| S7 | 注释与代码不一致 2 处 | `CanvasWidget.cpp:444`、`tests/GeometrySmoke.cpp:2` | ① 改注释为"用不同颜色表示不可自由拖动"，或真的改成空心标记；② 把 `@file` 改为 `tests/GeometrySmoke.cpp` |
| S8 | `src/model/Commands.h:22` 引入 `#include <QUndoCommand>`，使 model 层带上 QtWidgets 依赖 | `src/model/Commands.h:22`、`tests/GeometrySmoke.pro:24`、`:31-34` | **记录取舍，暂不改**：撤销命令对象是"编辑操作"的抽象，放在 model 层让 controller 只做编排（见 design.md 模块划分表）。工程上它确实引入了 QtWidgets 依赖，因此几何自测工程**刻意不编译 `Commands.cpp`**、只用 `QT += core`（`tests/GeometrySmoke.pro:24`、`:31-34`），以此证明"几何内核零界面依赖"这一更强命题；撤销栈的正确性由 06 阶段在 GUI 里人工验证。若将来需要把几何内核独立成库，再把命令对象上移到 controller 层即可 |
| S9 | 圆弧目前只支持三点弧（起点、弧上一点、终点） | `ToolId.h:25`、`MainController.cpp:436-469` | 记录为已知限制（`docs/delivery.md` 已知缺陷 T5 已登记）。需求 F7 只要求三点弧，故不违反需求；若要支持"圆心 + 两点"需扩展 `ArcShape` 的构造方式与文件格式 |
| **S10 ✅ 已修复** | 主 `.pro` 曾 `include(tests/geometry_test.pri)`，而该文件已随"测试拆为独立工程"被删除；实测每次 qmake 报 `Cannot read …: No such file or directory`（退出码 0，不影响构建） | 原 `GeoBoard.pro:107`（及上方 `:104-106` 的说明注释） | **已删除**第 107 行及对应注释，原位（现 `GeoBoard.pro:97-107`）改为说明"几何引擎自测不在本工程内、它是独立工程 `tests/GeometrySmoke.pro`"的注释；`scripts\build.bat build` 实测 qmake 不再输出 `Cannot read …`，`qt_verify.py` 的 `.pro` 引用完整性为 3 个 `.pro`、41 条引用全部存在。教训保留：`qt_verify.py` 的引用完整性检查不覆盖 `include()`，此类问题只能靠人工发现 |
| **S11 ✅ 已消除** | `qt_verify.py` 曾报"构建垃圾残留 29 个文件"，全部位于交付打包目录 `GeoBoard-0.1.0-win64\`（`windeployqt` 收集的 Qt/MSVC 运行库与 exe） | 原 `GeoBoard-0.1.0-win64\`、`.gitignore:57-59` | **已消除**：交付产物移出项目目录（放到 `C:\Users\a\Desktop\functino\` 下，与 `dist/` 同性质），复核运行该项输出"工作区干净"、交付结论"机械校验全部通过"。`.gitignore` 中的 `dist/`、`GeoBoard-*-win64/`、`*.zip` 规则保留，防止将来误提交 |
| S12 | `JsonStore.h:77-78` 的契约"失败时不得改动 document"在第 560-575 行的防御分支下不成立（该分支先 `clear()` 并部分填充后才失败） | `src/service/JsonStore.h:77-78`、`JsonStore.cpp:556-575` | 该分支不可达（id 已在前面的循环里校验为正整数且互不重复），但契约表述过强：把注释限定为"在文件内容被校验通过之前不改动 document"，或把 `addShape` 的失败处理改为在提交前完成全部可行性检查 |

## 5. 已确认无需修改

| 项 | 原因 |
|---|---|
| **交点参数约定**（`GeoMath::intersectLineCircle` 的解参数 vs `LineData::containsParameter` 的参数区间） | 最终结论：**原实现正确，无需修改**。`intersectLineCircle`（`GeometryMath.cpp:187-232`）以方向向量长度的平方作为二次项系数解方程，得到的参数恰好与 `LineData::clampParameter/containsParameter`"t=1 落在 `origin + direction`"的约定一致；我一度误判为"两套约定不同"并改过求解器，被测试用例（半径 3 的圆与 [0,8] 线段应只有 1 个交点、横跨圆的线段应有 2 个交点，见 `tests/GeometrySmoke.cpp:335`/`:345`）与诊断程序证伪后已回退。**加固措施**：新增 `LineData::parameterAt()`（`Shape.h:114-122`、`Shape.cpp:50-59`）作为"点 → 参数"的唯一换算入口，调用点一律经它换算（`GeoDocument.cpp:225`、`:717`、`Shape.cpp:170`），并在注释里写明曾经的误判，避免再次踩坑 |
| 退化（无效）图元只标记、不删除 | design.md 决策"退化处理"：删除会让"拖过退化位形"不可逆地丢图元，标记后拖回即自动恢复；已被测试覆盖（`tests/GeometrySmoke.cpp:531-536`（弧退化与恢复）、`:366-373`（交点失效与恢复），行号为修复后复核重测） |
| 未引入 `QSettings`（窗口尺寸/最近目录记忆） | 需求未要求，已在 design.md 未决问题与 `docs/delivery.md` 第 3 节登记，避免范围无声膨胀 |
| 未引入工作线程 | 没有长耗时操作（最大量级是 1000 图元文本的读写），线程只会增加生命周期风险 |
| `GeometrySmokeTest` 通过 `friend` 读取 `GeoDocument` 内部索引 | 仅测试用途，用于直接证实"父 id < 自身 id"这一增量重算正确性的前提（`GeoDocument.h:263-268` 已注明"生产代码不得依赖"） |
| 测量值只读、不参与后续作图 | 需求 N7 明确不做：需要约束求解器，超出本期范围 |
| F15"循环依赖拒绝"没有显式提示路径 | 由构造方式保证：所有工具都只能**新建**图元，新图元的父 id 必然小于自身（`GeoDocument.cpp:404-407` 用 `Q_ASSERT` 固化这一不变式，加载期由 `JsonStore.cpp:880-887` 校验），界面上无法表达"让 A 反过来依赖由 A 决定的对象"，因此不存在需要拒绝的操作。建议把 F15 的验收步骤改写为"尝试构造循环（在界面上不可达），并确认文档拓扑序不变式成立" |

## 6. 审查与构建过程中发现并已修复的真实缺陷

这些缺陷都是**实测复现**后修掉的，记录在此供后续维护参考。

| 编号 | 症状 | 根因 | 修法 | 位置 |
|---|---|---|---|---|
| D1 | 以鼠标为锚点滚轮缩放时，视图**缓慢漂移**，鼠标下的那个点无法保持不动 | `Camera::zoomAround()` 的中心补偿量方向写反（y 分量符号错误） | 改为"x 加、y 减"：`m_sceneCenter.x += deltaX; m_sceneCenter.y -= deltaY;`（场景 y 向上、屏幕 y 向下的翻转只在本类做一次） | `src/model/Camera.cpp:60-78` |
| D2 | MinGW 严格模式下编译失败（`M_PI` 未定义）；同一份代码在 MSVC 下却正常 | `M_PI` 不是 C++11 标准宏，是否提供取决于实现（MinGW 的 `<cmath>` 在严格模式下不提供） | 定义项目自己的常量 `GeoMath::kPi / kTwoPi` 并全量替换 | `src/model/GeometryMath.h:133-136`、`GeometryMath.cpp:16-18`、`Shape.cpp:18`、`:739`、`CanvasWidget.cpp:595` |
| D3 | `QStringLiteral(APP_VERSION)` 编译失败，报错却指向 `QString` 内部，极难定位 | `.pro` 里 `DEFINES += APP_VERSION=\"0.1.0\"` 的引号要经 qmake → 响应文件 → cl.exe **三层转义**，实测退化成裸的 `0.1.0`，不是字符串字面量 | 版本号改在 `src/version.h` 里定义 `GEOBOARD_VERSION`（标准 C++ 字面量，不依赖命令行转义），`.pro` 只留 `APP_NAME` | `src/version.h:19-23`、`GeoBoard.pro:59-67` |
| D4 | 编译期报"没有成员/没有匹配的重载"，但 API 看起来完全正常 | 用了 **Qt 5.14+** 才有的 API，而本项目基准是 Qt 5.12.12：① `QSet` 的范围构造函数；② `QAction::setAccessibleName`（该接口属于 `QWidget`，`QAction` 一直没有）；③ `QWidget::screen()` | ① 显式遍历 insert（`CanvasWidget.cpp:766-773`）；② 不再给 `QAction` 设无障碍名，改用文本 + `toolTip/statusTip` 提供（`GeoBoardMainWindow.cpp:129-135`）；③ 未使用 `screen()`，需要屏幕信息时由 `QWidget` 自身接口取得 | `CanvasWidget.cpp:766-773`、`GeoBoardMainWindow.cpp:129-135` |
| D5 | 链接报 `LNK2005: main 已经在 main.obj 中定义` + `LNK1169: 找到一个或多个多重定义的符号` | 主工程与测试工程曾是**同一个 `.pro` 里的两个 app 目标**，obj 都写进同一个 `release/`，两边的 `main` 撞车 | 把几何自测拆成独立工程 `tests/GeometrySmoke.pro`，拥有自己的构建目录（`build-test/`），并删除 `.pro` 里的第二个目标 | `tests/GeometrySmoke.pro:8-13`、`GeoBoard.pro:97-107`（残留的 `include` 行已随 S10 修复删除，现为说明注释） |
| D6 | 子工程构建时 qmake 报 `WARNING: Failure to find …`，nmake 随后报 `U1073` | `.pri` 被 `include()` 时 `$$PWD` 指向 **`.pri` 自己所在的目录**（`tests/`），而路径仍按 app 的 `src/` 前缀书写 | 放弃 `.pri` 方案，改为独立工程 + 独立构建目录（与 D5 同一处修改） | `tests/GeometrySmoke.pro:1-19` |
| D7 | `build.bat` 里的中文注释导致命令行在多字节字符处被截断，构建命令执行到一半就失效 | 脚本以 UTF-8 保存中文，而 cmd.exe 按 OEM 代码页（简体中文 Windows 为 GBK/936）读取 `.bat` | 把 `scripts/build.bat` 改为**纯 ASCII**（注释也用英文），并在文件头写明"必须保持 ASCII"的原因 | `scripts/build.bat:1-24` |
| D8 | qmake 报 `Unexpected UTF-8 BOM`，`.pro` 解析失败 | 用 PowerShell `Set-Content -Encoding UTF8` 写文件会**加 BOM**（PowerShell 5.1 的 `UTF8` 是带 BOM 的编码） | 清除全部文件的 BOM（含 `.pro`、`.h`、`.cpp`），源文件统一为 **UTF-8 无 BOM**，并在 `.pro` 里加 `/utf-8`（缺它则中文注释触发 C1075） | `GeoBoard.pro:41-57`、`docs/requirements.md` 第 6 节"编码约束" |
| D9 | 圆面积标注**存得下、打不开**：含圆面积标注的 `.gboard` 文件保存正常，重新打开时被拒绝（报关联图元类型不符） | `JsonStore` 加载测量标注时把 `Area` 的关联图元类型**写死为 `Polygon`**；而同一轮修复已让"面积"在模型里同时支持圆（πr²）与多边形（鞋带公式），两处语义不一致——这类缺陷只有真正走一遍 save → load 才会暴露 | 按测量类型分派关联图元的校验：`SegmentLength` 接受 `Line`；`Radius` / `Circumference` 接受 `Circle`；**`Area` 接受 `Polygon` 或 `Circle`** | `src/service/JsonStore.cpp:1350-1383` |
| D10 | 磁盘上多出两个不在 `.pro` 里的源文件，却定义了与正式实现**同名的类 `GeoBoardMainWindow`**，且与正式头文件共用**同一个包含保护宏 `GEOBOARDMAINWINDOW_H`**（一旦加入工程即类重定义 / 链接冲突） | 脚手架生成后未清理的残留文件（审查项 27、37 → R1） | 删除 `src/view/MainWindow.h` 与 `src/view/MainWindow.cpp`；`src/view/` 只剩 `CanvasWidget.{h,cpp}` 与 `GeoBoardMainWindow.{h,cpp}` | 原 `src/view/MainWindow.h`、`src/view/MainWindow.cpp`（**已删除**） |
| D11 | 每次 qmake 都报 `Cannot read tests/geometry_test.pri: No such file or directory`（退出码 0，构建不受影响，因而容易被长期忽略） | 主 `.pro` 仍 `include()` 一个在"测试拆为独立工程"时已删除的 `.pri`，且 `qt_verify.py` 的引用完整性检查**不覆盖 `include()` 目标**，脚本报不出（审查 S10） | 删除该 `include` 行及对应注释，原位改为说明"测试是独立工程、不放在本 .pro 内"的注释 | 原 `GeoBoard.pro:107`（现 `:97-107` 为说明注释） |
| D12 | 工具栏在常见窗口宽度下排不下 22 个文字按钮，被挤出可见区的 8 个测量工具**完全无法点击**（截图实测：1600px 宽窗口里工具按钮只显示到"交点"） | 22 个文字按钮挤在**一行**工具栏里；而 `QToolBar` 在 `setMovable(false)` 时**不显示溢出按钮**，被挤到可见区之外的工具既看不到也点不到——这是**可用性缺陷**而非美观问题 | 在 `GeoBoardMainWindow::createToolBar()` 里把工具栏拆成**两行**：第一行"作图工具"（选择 + 点 / 对象上的点 / 线段 / 射线 / 直线 / 圆 / 圆弧 / 多边形 + 中点 / 垂线 / 平行线 / 角平分线 / 交点，共 **14 个**），第二行"测量工具"（距离 / 线段长度 / 角度 / 圆半径 / 圆周长 / 多边形面积 / 圆面积 / 点坐标，共 **8 个**）。要点：`addToolBarBreak()` **必须放在 `addToolBar()` 之前**——它插入的是"下一个工具栏另起一行"的分隔，放在之后等于没生效（第一版写错，由截图发现并纠正） | `src/view/GeoBoardMainWindow.cpp:270-321`（`addToolBarBreak()` 在 `:285`）；验证证据：`docs/screenshot-toolbar.png`（本轮新增的真实截图，两行工具按钮完整显示、无溢出） |

## 7. 阶段判据核对

| 判据（`verification.md` 第五节） | 状态 | 依据 |
|---|---|---|
| `qt_verify.py` 无 FAIL 项 | 满足 | 修复后复核运行 `结果: OK  (OK=15 INFO=0 WARN=0 FAIL=0)`，退出码 0 |
| 41 条人工审查项全部有结论 | 满足 | 第 2 节（通过 34、部分通过 1、不通过 6，均带位置与行号） |
| 所有"必须修复"项已修复，并重跑 `qt_verify.py` 与构建 | **现已满足（修复后复核）** | R1、S10 已关闭，R2 的 F20/F21/F23 已关闭；仅 F42 子项遗留（不影响本判据的"必须修复项"口径，见第 3 节"残留说明"）。本轮重跑：`scripts\build.bat build` 通过、`scripts\build.bat test` 48 项断言 0 失败（退出码 0）、`scripts\build.bat json` 19 项断言 0 失败（退出码 0）、`qt_verify.py` 无 FAIL 也无 WARN |
| `docs/review.md` 已创建 | 满足 | 本文档 |
| `docs/design.md` 与实际代码一致 | **未满足（与 R1/R2/S10 无关，本轮未处理）** | 见审查项 7、8 与 S3：4.3 的 `modifiedChanged(bool)` 行在代码中不存在（实为 `QUndoStack::cleanChanged` 驱动）、4.2 缺 `fitContentRequested`、4.4 的 `showStatusMessage` 实为 `setStatusMessage` 且缺 `setFileName`（`docs/design.md:102`、`:115`）。本轮三项修复没有改动契约表，问题仍在 |
