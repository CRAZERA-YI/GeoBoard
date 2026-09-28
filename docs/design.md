# GeoBoard 设计文档

> **更正块（第三轮，四项新功能后追加）**
>
> 新增三个模块与一处模型扩展。**分层与依赖方向未变**（仍是 `main → controller → model / service / view` 单向）。
>
> | 新增 | 所属层 | 职责 | 依赖 |
> |---|---|---|---|
> | `ArcKind` + `ArcShape` 第二构造函数 | model | 两种圆弧构造方式共用同一份 `ArcData`，因此绘制、求交、附着点投影**只有一份实现** | 无新依赖 |
> | `view/SceneExporter` | view | PNG / SVG 导出 | 依赖 `CanvasWidget::renderToDevice()` |
> | `CanvasWidget::snapScenePos()` + `SnapResult` | view | 吸附求解（已有点 → 栅格 → 曲线） | 读 `GeoDocument` 与 `Camera` |
> | `view/ShapeListPanel` | view | 对象清单与双向选中同步 | 只读 `GeoDocument` + 改选中集 |
>
> **三条值得写下来的设计决定**：
>
> 1. **导出不写第二套绘制代码。** `CanvasWidget::renderToDevice()` 是"画到任意
>    QPaintDevice"的唯一入口，屏幕与导出都走它，因此二者不会分叉。导出时临时把相机
>    视口切到目标尺寸再恢复——这样"适配内容 / 网格步长 / 线宽像素"都按导出尺寸自洽计算，
>    而不是把屏幕那幅拉伸放大。
> 2. **导出放在 view 层而不是 service 层。** 它必须认识 `CanvasWidget`；
>    `service/JsonStore` 只处理数据、不认识任何控件。分层是按"认识谁"划的，
>    不是按"像不像工具"划的。
> 3. **停靠面板不改文档的图元，只改选中集。** 删除、聚焦都以信号交给控制器——
>    这样删除仍然进撤销栈、仍然有级联提示。面板若自己删就会绕开撤销机制，
>    而且两条删除路径早晚会分叉。
>
> **吸附为什么创建"附着点"而不是自由点**：`snapScenePos()` 在命中曲线时返回宿主曲线 id，
> 控制器据此建 `PointKind::OnObject` 的点。这是把"看起来在线上"变成**结构性约束**——
> 拖动时该点只会沿曲线滑动。若只把坐标吸到曲线上、仍建自由点，用户一拖它就掉下来了。

> 阶段 03 产物。本文档定义模块划分、关键类、信号槽契约与关键决策。
> 编码阶段（04）的每一行代码都必须能对应到本文档；发现设计不可实现时，
> **先改本文档再改代码**（见 `qt5_skill/workflow/pipeline.md` 回退规则）。

## 1. 需求摘要

解决"讲几何时图形是死的、拖动一下整个图形关系就散了"的问题：做一个能把**作图关系**（而非
画好的线条）记录下来的动态几何画板，拖动自由点时所有依赖它的图形按几何关系自动重算。

核心功能：点/线段/射线/直线/圆/三点弧/多边形等基础作图；中点、垂线、平行线、角平分线、
交点等派生约束；距离/长度/角度/半径/周长/面积/坐标等实时测量；`.gboard` 文件的保存与打开；
工具栏、状态栏、撤销重做、缩放平移、网格坐标轴、选中删除。

**不做**：QML/Qt6/CMake/非 Windows 平台、三维几何、定理证明与符号计算、测量值驱动的约束、
导出图片与打印、多文档标签页、安装包。完整清单见 `docs/requirements.md` 第 4 节。

## 2. 模块划分

| 层 | 目录 | 本项目中的具体职责 | 关键类 |
|---|---|---|---|
| Model | `src/model/` | 保存图元集合与父子引用；维护"id 递增即拓扑序"的依赖图；只重算受影响子图；求解两曲线交点；计算中点/外接圆/角平分线/投影；维护退化（无效）标记；维护选中集；提供撤销/重做命令对象 | `GeoDocument`、`Shape` 及其子类、`GeoMath`、`Camera`、`AddShapesCommand` |
| View | `src/view/` | 装配工具栏/菜单/状态栏；自绘画布（网格、坐标轴、图元、点名与测量标注、作图预览、选中高亮）；把鼠标、滚轮、键盘事件换算为**场景坐标**并发出语义化信号；提供文件对话框与错误/询问对话框 | `GeoBoardMainWindow`、`CanvasWidget` |
| Controller | `src/controller/` | 工具状态机（每种工具需要几次点击、点击后建什么图元）；把画布点击翻译为文档改动；命中测试与拖动决策；撤销栈压栈时机；文件动作编排；状态提示文案 | `MainController`、`ToolId` |
| Service | `src/service/` | `.gboard` 文件的 JSON 读写；格式版本校验；加载时按依赖序重建图元（父 id 必须小于自身 id，否则拒绝）；用 `QSaveFile` 原子写入 | `JsonStore` |

依赖方向单向：`main → controller → model / service / view`。Model 不含任何界面头文件；
View 不写业务规则、不做文件 IO、不 `new` Model/Service；Controller 不碰 View 内部控件。

**几何由点位 id 导出，不缓存坐标**：线段存两个点 id，圆存圆心点与圆上点 id。于是
"拖动点 → 半径变化 → 依赖图形跟随"是数据结构的自然结果，不需要任何同步代码。
唯一例外是测量值，它是派生量，每次重算都重新读取输入点位的当前坐标。

**重算是子图级的**：每条派生图元记录 `parents()`，且约定父 id 必然小于自身 id
（正常构建路径下成立，因此 id 递增序天然是拓扑序）。`recomputeFrom(changedIds)`
只收集传递依赖 `changedIds` 的图元并按 id 升序重算。**退化是一等状态**：父点位非有限、
三点共线导致外接圆不存在、两曲线无交点，都标记该图元 `isValid() == false`——不绘制、
不参与交点求解，但**保留在文档中**，这样拖动经过退化位形时可以自动恢复。

## 3. 关键类

| 类名 | 所属层 | 职责（一句话） | 依赖谁 | 被谁依赖 |
|---|---|---|---|---|
| `GeoMath`（命名空间） | Model | 无状态的向量运算与几何求解：交点、投影、外接圆、角平分线、面积、点线距 | — | `Shape`、`GeoDocument`、`MainController` |
| `Shape` | Model | 图元的抽象基类：id、类型、父 id 列表、有效性、自身几何推导 | `GeoMath` | `GeoDocument` |
| `PointShape` | Model | 点：自由点可任意移动，附着点投影到父曲线上 | `Shape`、`GeoMath`、`GeoDocument`（只读取父点位） | `GeoDocument` |
| `CurveShape` | Model | 曲线的抽象中间层：统一提供"求与另一曲线的交点"接口 | `Shape`、`GeoMath` | `IntersectionPointShape` |
| `LineShape` | Model | 线段 / 射线 / 直线（同一类，区分有界维度）与垂线 / 平行线 / 角平分线 | `Shape`、`GeoMath` | `GeoDocument` |
| `CircleShape` / `ArcShape` | Model | 圆（圆心+圆上点）与三点弧（外接圆 + 角度区间） | `Shape`、`GeoMath` | `GeoDocument` |
| `PolygonShape` | Model | 多边形顶点列与面积 | `Shape`、`GeoMath` | `GeoDocument` |
| `MidPointShape` / `IntersectionPointShape` | Model | 派生点：两点中点、两曲线交点 | `Shape`、`GeoMath` | `GeoDocument` |
| `MeasurementShape` | Model | 只读标注：距离/长度/角度/半径/周长/面积/坐标，重算时读取输入点位 | `Shape`、`GeoMath`、`GeoDocument`（只读） | `GeoDocument` |
| `GeoDocument` | Model | 图元容器、父子索引、子图重算、增删（含级联删除）、选中集、序列化取数 | 各 `Shape` 子类 | `JsonStore`、`MainController`、`CanvasWidget`（只读） |
| `Camera` | Model | 场景 ↔ 屏幕坐标变换、缩放平移、可见场景矩形、网格步长选取 | — | `CanvasWidget`、`MainController` |
| `AddShapesCommand` / `RemoveShapesCommand` / `MovePointCommand` | Model | `QUndoCommand` 子类：增删图元、移动点（拖动结束时压栈一次） | `GeoDocument` | `MainController` |
| `JsonStore` | Service | `.gboard` 读写、版本校验、id 重映射、原子写入、错误信息 | `GeoDocument`、各 `Shape` 子类 | `MainController` |
| `CanvasWidget` | View | 画布自绘与事件换算；只发语义化信号，不改文档 | `GeoDocument`（只读）、`Camera` | `GeoBoardMainWindow`、`MainController` |
| `GeoBoardMainWindow` | View | 窗口装配：工具栏、菜单、状态栏、画布；提供文件对话框与提示 | `CanvasWidget`、`ToolId` | `MainController`、`main.cpp` |
| `MainController` | Controller | 工具状态机、画布事件 → 文档改动、撤销栈、文件动作、状态提示 | `GeoBoardMainWindow`、`GeoDocument`、`JsonStore`、`Camera` | `main.cpp` |

依赖方向检查：Model 行无 View 类名 ✓；Service 行无 View/Controller 类名 ✓。

## 4. 信号槽契约

### 4.1 `CanvasWidget` → `MainController`

| 发送方 | 信号 | 接收方 | 槽 | 触发时机 | 参数含义 |
|---|---|---|---|---|---|
| `CanvasWidget` | `canvasClicked(QPointF)` | `MainController` | `onCanvasClicked` | 画布左键单击 | 单击位置的**场景坐标**（已由 Camera 换算，非屏幕像素） |
| `CanvasWidget` | `canvasDoubleClicked(QPointF)` | `MainController` | `onCanvasDoubleClicked` | 画布左键双击 | 同上；用于结束多边形绘制 |
| `CanvasWidget` | `mouseScenePosChanged(QPointF)` | `GeoBoardMainWindow` | `onMouseScenePosChanged` | 鼠标移动 | 鼠标位置对应的场景坐标，供状态栏显示 |
| `CanvasWidget` | `pointDragStarted(ShapeId)` | `MainController` | `onPointDragStarted` | 在某点上按下左键准备拖动 | 被拖动的点图元 id（仅自由点与附着点可拖） |
| `CanvasWidget` | `pointDragged(ShapeId, QPointF)` | `MainController` | `onPointDragged` | 拖动中（每次移动） | 点 id 与新的场景坐标 |
| `CanvasWidget` | `pointDragFinished()` | `MainController` | `onPointDragFinished` | 松开左键 | 无参数；此刻才把整段拖动压入一次撤销命令 |
| `CanvasWidget` | `selectionChanged(QList<ShapeId>)` | `MainController` | `onSelectionChanged` | 选中集变化 | 新的选中图元 id 列表（可能为空） |
| `CanvasWidget` | `deleteRequested()` | `MainController` | `onDeleteRequested` | 按 Delete 键 | 无参数；删除目标是画布当前的选中集 |
| `CanvasWidget` | `escapeRequested()` | `MainController` | `onEscapeRequested` | 按 Esc 键 | 无参数；取消未完成的点击序列 |
| `CanvasWidget` | `zoomRequested(double, QPointF)` | `MainController` | `onZoomRequested` | 滚轮滚动或缩放动作 | 缩放**倍数**（>1 放大）与缩放**锚点**的场景坐标 |
| `CanvasWidget` | `panRequested(QPointF)` | `MainController` | `onPanRequested` | 中键拖拽 | 本次平移的**场景坐标增量** |

### 4.2 `GeoBoardMainWindow` → `MainController`

| 发送方 | 信号 | 接收方 | 槽 | 触发时机 | 参数含义 |
|---|---|---|---|---|---|
| `GeoBoardMainWindow` | `toolChangeRequested(ToolId)` | `MainController` | `onToolChangeRequested` | 工具栏点击或快捷键 | 目标工具枚举值 |
| `GeoBoardMainWindow` | `gridVisibilityToggled(bool)` | `MainController` | `onGridVisibilityToggled` | 视图菜单勾选 | 是否显示网格 |
| `GeoBoardMainWindow` | `axesVisibilityToggled(bool)` | `MainController` | `onAxesVisibilityToggled` | 视图菜单勾选 | 是否显示坐标轴 |
| `GeoBoardMainWindow` | `labelsVisibilityToggled(bool)` | `MainController` | `onLabelsVisibilityToggled` | 视图菜单勾选 | 是否显示点名与测量标注 |
| `GeoBoardMainWindow` | `newRequested()` | `MainController` | `onNewRequested` | 文件→新建 | 无参数；未保存时由 Controller 先询问 |
| `GeoBoardMainWindow` | `openRequested(QString)` | `MainController` | `onOpenRequested` | 文件→打开 | `.gboard` 文件路径；**空字符串**表示由 View 弹对话框询问 |
| `GeoBoardMainWindow` | `saveRequested(QString)` | `MainController` | `onSaveRequested` | 文件→保存 / 另存为 | 目标路径；**空字符串**表示"另存为"需询问新路径 |
| `GeoBoardMainWindow` | `undoRequested()` / `redoRequested()` | `MainController` | `onUndoRequested` / `onRedoRequested` | 编辑菜单或快捷键 | 无参数 |
| `GeoBoardMainWindow` | `zoomInRequested()` / `zoomOutRequested()` / `zoomResetRequested()` | `MainController` | `onZoomIn` / `onZoomOut` / `onZoomReset` | 视图菜单 | 无参数；缩放锚点为画布中心 |
| `GeoBoardMainWindow` | `aboutRequested()` | `MainController` | `onAboutRequested` | 帮助→关于 | 无参数 |

### 4.3 `GeoDocument` → 观察者

| 发送方 | 信号 | 接收方 | 槽 | 触发时机 | 参数含义 |
|---|---|---|---|---|---|
| `GeoDocument` | `documentChanged()` | `CanvasWidget` | `update()`（通过 lambda 或槽） | 图元增删或重算之后 | 无参数；画布只做整体重绘 |
| `GeoDocument` | `modifiedChanged(bool)` | `GeoBoardMainWindow` | `onModifiedChanged` | 未保存标记变化 | 是否有未保存改动，用于标题栏 |

（`GeoDocument` 继承 `QObject` 只为发信号；它的几何逻辑不依赖 `QApplication`，
可在无 GUI 的测试程序里直接构造。）

### 4.4 `MainController` → View（**Controller 只调用下列公开方法，不碰控件成员**）

| 方法 | 作用 |
|---|---|
| `canvas()` | 取画布指针，供 Controller 连接画布信号 |
| `setActiveTool(ToolId)` | 同步工具栏选中态与状态提示 |
| `setPreview(const QList<QPointF> &points, const QString &hint)` | 设置作图预览（橡皮筋 / 预览圆 / 已定顶点）与提示 |
| `clearPreview()` | 清除预览 |
| `showStatusMessage(const QString &, int timeoutMs)` | 状态栏临时提示 |
| `setMouseStatus(const QString &)` | 状态栏坐标文本 |
| `setModified(bool)` | 标题栏未保存标记 |
| `setUndoRedoEnabled(bool canUndo, bool canRedo)` | 撤销/重做菜单项可用性 |
| `setZoomPercent(int)` | 状态栏缩放百分比 |
| `promptOpenPath()` / `promptSavePath(const QString &suggested)` | 文件对话框（属于 View 职责） |
| `showError(const QString &title, const QString &detail)` | 错误提示对话框 |
| `askSaveChanges()` | 三态询问：保存 / 不保存 / 取消 |
| `showAbout()` | 关于对话框 |

状态栏分工：随工具变化的操作提示由 Controller 写；鼠标坐标与缩放百分比由 View 自己写。

## 5. 关键决策

| 决策 | 选择 | 放弃了什么 | 为什么 |
|---|---|---|---|
| 界面构建 | 纯 C++ 代码 | `.ui` 文件 | skill 硬性边界；`.ui` 无法被准确 review 与修改，`qt_verify.py` 会判 FAIL |
| 几何表示 | 由**点位 id 导出**，不缓存坐标 | 每个图元缓存自己的坐标并在拖动时同步 | 缓存必然出现"忘了同步"的 bug；导出式让动态性成为数据结构的自然结果 |
| 拓扑序 | 建图时保证"父 id < 自身 id"，据此免去运行时排序 | 每次重算做一次 Kahn 排序 | 重算只做一次线性扫描；非法顺序在加载期直接拒绝并报错 |
| 退化处理 | 标记 `isValid()==false`，**不删除** | 退化即删除图元 | 删除会让"拖过退化位形"不可逆地丢图；标记可自动恢复 |
| 重算范围 | 只重算受影响子图 | 每次拖动全量重算 | 全量重算在 1000 图元下会掉帧；需求要求 50ms 内跟随 |
| 撤销实现 | `QUndoStack` + 命令对象 | 保存整份文档快照做全量回滚 | 命令对象内存开销小、语义清晰；快照在 1000 图元下每次操作都要复制一遍 |
| 删除语义 | 级联删除依赖它的图元，并提示被删数量 | 只删选中的，留下悬空引用 | 悬空引用会让派生图元永久失效；级联删除 + 明确提示可预期，且可撤销 |
| 交点工具 | 一次创建两曲线的**全部**交点 | 让用户再点一次选择要哪一个 | 少一次交互；且拖动时交点数量本身会变化，逐点选择会失配 |
| 测量值 | 只读展示，不参与后续作图 | 测量值驱动约束（如"固定长度为 5"） | 后者需要约束求解器，远超本期范围；已列入 requirements 的「明确不做」 |
| 文件格式 | UTF-8 JSON 文本，带 `version` 字段 | 二进制格式 | 需求要求可人工查看与编辑；JSON 便于排查与版本迁移 |
| 高 DPI | 交给系统与环境变量，不在 `main()` 设属性 | 写死 `AA_EnableHighDpiScaling` | `ui_defaults.md` 第七节要求两种方式只选一种；自绘已用逻辑坐标 |
| 窗口记忆 | 本期不做 `QSettings` | 记住尺寸、位置与最近目录 | 需求未要求；列入未决问题，避免范围无声膨胀 |
| 命中检测 | 线性扫描 + 屏幕像素容差 | 四叉树等空间索引 | 1000 图元量级下线性扫描足够；若 07 阶段实测不足再引入 |
| 界面风格 | 经典 Windows 原生风格（布局 + `QStyle`） | QSS 换肤 / Fusion | 硬编码颜色会破坏深色主题，且用户无法通过系统设置改变 |

## 6. 未决问题

- [ ] 圆弧是否需要增加"圆心 + 两点"的构造方式（当前只做三点弧）
- [ ] 是否需要 SVG / PNG 导出（需求列为"不做"，但实现成本不高）
- [ ] 是否需要吸附对齐（网格吸附、对象吸附）
- [ ] 是否需要 `QSettings` 记忆窗口尺寸与最近打开目录（当前决策为不做）
- [x] 是否需要"测量值参与作图"？→ 不做，需要约束求解器，列入 requirements「明确不做」
- [x] 网格与坐标轴是否默认显示？→ 默认显示，视图菜单可关闭
