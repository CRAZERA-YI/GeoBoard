/**
 * @file    view/CanvasWidget.h
 * @brief   作图画布：自绘网格、坐标轴、图元与预览，并把鼠标事件翻译为语义化信号。
 * @details 画布是"会画图的控件"，不是"会思考的模块"。它对文档**只读**，
 *          对工具**只知道当前是哪个工具**，其余一切都表达为信号交给控制器：
 *          - 鼠标位置一律换算成**场景坐标**后再发出（屏幕像素不出本类）；
 *          - "点被拖动"这类意图发信号，由控制器决定是否改动文档；
 *          - 唯一的例外是选中集：选择工具下点击即直接更新文档的选中集
 *            （选中集属于"界面状态"，由画布直接维护最内聚），随后发信号通知控制器。
 *
 *          不负责：创建图元、判断工具语义、文件读写、撤销栈。
 */

#ifndef GEOBOARDCANVASWIDGET_H
#define GEOBOARDCANVASWIDGET_H

#include "controller/ToolId.h"
#include "model/Camera.h"
#include "model/GeoDocument.h"
#include "model/Shape.h"

#include <QList>
#include <QPointF>
#include <QWidget>

/**
 * @brief 几何作图画布。
 */
class CanvasWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief 构造画布。
     * @param parent 父控件。
     */
    explicit CanvasWidget(QWidget *parent = nullptr);

    /** @brief 析构画布。 */
    ~CanvasWidget() override;

    /**
     * @brief 绑定要显示的文档。
     * @param document 文档指针，**不拥有**；传 nullptr 表示无文档。
     * @note  绑定后会连接 documentChanged/selectionChanged 以自动重绘。
     */
    void setDocument(GeoDocument *document);

    /** @brief 返回当前文档（可能为 nullptr）。 */
    GeoDocument *document() const { return m_document; }

    /** @brief 返回相机（控制器用它做缩放、平移与适配）。 */
    Camera *camera() { return &m_camera; }

    /** @brief 返回相机的只读引用。 */
    const Camera *camera() const { return &m_camera; }

    /** @brief 设置当前工具（影响鼠标行为，例如是否允许拖动点、是否显示预览）。 @param tool 工具枚举。 */
    void setActiveTool(ToolId tool);

    /** @brief 返回当前工具。 */
    ToolId activeTool() const { return m_activeTool; }

    /**
     * @brief 设置作图预览（橡皮筋）。
     * @param points 已确定的顶点（场景坐标，按点击顺序）。
     * @param hint 当前鼠标位置对应的活动点；不显示时传一个极大值点或使用 clearPreview()。
     * @param showCircle true 表示把"已定点 → 活动点"解释为半径并画出预览圆（画圆工具用）。
     */
    void setPreview(const QList<QPointF> &points, const QPointF &hint, bool showCircle = false);

    /** @brief 清除作图预览。 */
    void clearPreview();

    /**
     * @brief 设置需要额外高亮的图元（作图过程中"已选中的基准曲线"）。
     * @param id 图元 id；kInvalidShapeId 表示取消高亮。
     * @note  与鼠标悬停高亮是两个独立通道：悬停是"鼠标下面是什么"，
     *        这里是"本工具已锁定的输入是什么"，垂线/交点等工具靠它给用户反馈。
     */
    void setHighlight(ShapeId id);

    /** @brief 返回当前额外高亮的图元 id。 */
    ShapeId highlight() const { return m_highlightId; }

    /** @brief 设置是否显示网格。 */
    void setGridVisible(bool visible);

    /** @brief 查询网格是否可见。 */
    bool isGridVisible() const { return m_gridVisible; }

    /** @brief 设置是否显示坐标轴。 */
    void setAxesVisible(bool visible);

    /** @brief 查询坐标轴是否可见。 */
    bool isAxesVisible() const { return m_axesVisible; }

    /** @brief 设置是否显示点名与测量标注。 */
    void setLabelsVisible(bool visible);

    /** @brief 查询标注是否可见。 */
    bool isLabelsVisible() const { return m_labelsVisible; }

    /**
     * @brief 设置是否启用吸附对齐。
     * @param enabled true 表示把作图输入吸附到栅格、已有点或曲线上。
     */
    void setSnapEnabled(bool enabled);

    /** @brief 查询吸附是否启用。 */
    bool isSnapEnabled() const { return m_snapEnabled; }

    /**
     * @brief 吸附结果。
     * @details 三个字段共同描述"用户想点的位置最终落在哪里"：
     *          `snapped` 说明是否发生了吸附；`hostCurveId` 非空表示该位置落在某条
     *          曲线上，控制器据此创建**附着点**（而不是自由点），于是点会一直贴在那条线上。
     */
    struct SnapResult
    {
        QPointF position;               ///< 吸附后的场景坐标。
        bool snapped;                   ///< 是否发生过吸附。
        ShapeId hostCurveId;            ///< 命中的曲线 id；未命中为 kInvalidShapeId。

        /** @brief 构造一个"不吸附"的结果。 */
        SnapResult() : position(), snapped(false), hostCurveId(kInvalidShapeId) {}
    };

    /**
     * @brief 对给定的场景坐标求解吸附位置。
     * @param scenePos 鼠标所在的场景坐标。
     * @return 吸附结果；未启用吸附时原样返回。
     * @note  优先级：**已有点 → 栅格 → 曲线**。点优先，因为"复用已有的点"是作图里
     *        最高频的意图；栅格优先于曲线，是为了保证栅格作图的确定性——否则靠近曲线时
     *        位置会被曲线悄悄拽走，用户会觉得"点不准"。
     */
    SnapResult snapScenePos(const QPointF &scenePos) const;

    /**
     * @brief 设置"圆心 + 半径 + 弧上点"的预览（供圆心式圆弧工具使用）。
     * @param center 圆心场景坐标。
     * @param rim 半径点场景坐标（半径由此两点距离导出）。
     * @param arcPoint 弧上点场景坐标；与 rim 重合时按整圆预览。
     * @note  与 setPreview 分开：折线橡皮筋表达不了圆弧的形状，
     *        而用户在定弧的范围时最需要看到的恰恰是弧本身。
     */
    void setArcPreview(const QPointF &center, const QPointF &rim, const QPointF &arcPoint);

    /** @brief 清除圆弧预览。 */
    void clearArcPreview();

    /**
     * @brief 查询当前是否正在显示圆弧预览。
     * @return 显示中返回 true。
     * @note  供测试断言"预览确实进入了绘制路径"——只看截图容易把
     *        "没画出来"误判成"画得太淡看不见"。
     */
    bool hasArcPreview() const { return m_hasArcPreview; }

    /**
     * @brief 查询当前是否有折线预览。
     * @return 有预览点或有预览提示时返回 true。
     */
    bool hasPreview() const { return !m_previewPoints.isEmpty() || m_hasPreviewHint; }

    /**
     * @brief 查询是否已进入"按下-拖动"作图状态。
     * @return 已进入返回 true。
     * @note  供测试判断预览为何没出现：这个标记为 false 时，鼠标移动
     *        根本不会走到预览分支，画布上当然什么都看不到。
     */
    bool isConstructionArmed() const { return m_constructionArmed; }

    /**
     * @brief 查询是否正处于"按下-拖动"作图中。
     * @return 正在拖动作图中返回 true。
     */
    bool isConstructionDragging() const { return m_isConstructionDragging; }

    /**
     * @brief 让视图适配全部内容。
     * @return 文档中存在有效图元并成功调整视图时返回 true。
     */
    bool fitContent();

    /**
     * @brief 把当前场景绘制到任意 QPaintDevice 上（供导出 PNG / SVG 使用）。
     * @param device 目标绘制设备（QImage、QSvgGenerator 等），不得为 nullptr。
     * @param targetSize 目标尺寸；决定缩放比例。
     * @param drawBackground 是否先填充背景色（导出位图通常需要，导出 SVG 不需要）。
     * @return device 有效并完成绘制时返回 true。
     * @note  这是**导出功能与屏幕显示共用同一套绘制代码**的入口：网格、坐标轴、
     *        全部图元与标注都按同一套逻辑画出，因此导出结果与屏幕所见一致，
     *        不会出现"导出后少了点什么"这类偏差。
     * @note  绘制期间会临时改动相机的视口尺寸以匹配 targetSize，完成后恢复原值，
     *        因此调用方无需关心相机状态。
     */
    bool renderToDevice(QPaintDevice *device, const QSize &targetSize, bool drawBackground);

    /**
     * @brief 取当前内容的场景包围盒。
     * @param minCorner 输出参数：左下角。
     * @param maxCorner 输出参数：右上角。
     * @return 存在有效内容时返回 true。
     */
    bool contentBounds(Vec2 *minCorner, Vec2 *maxCorner) const;

signals:
    /** @brief 画布被左键单击。 @param scenePos 单击位置的场景坐标。 */
    void canvasClicked(const QPointF &scenePos);

    /** @brief 画布被左键双击。 @param scenePos 双击位置的场景坐标。 */
    void canvasDoubleClicked(const QPointF &scenePos);

    /** @brief 鼠标移动。 @param scenePos 鼠标位置的场景坐标。 */
    void mouseScenePosChanged(const QPointF &scenePos);

    /**
     * @brief 需要更新"圆心式圆弧"的分步预览。
     * @param center 已确定的点（第一步是圆心，第二步起仍是圆心）。
     * @param cursor 当前鼠标位置（场景坐标）。
     * @note  之所以让画布发信号、由控制器决定预览内容：预览要看**状态机走到第几步**
     *        （只定了圆心、还是半径也定了），而状态机归控制器所有。
     *        画布若自己猜第几步，判断逻辑就会有两份，早晚分叉。
     */
    void arcCenterPreviewNeeded(const QPointF &center, const QPointF &cursor);

    /** @brief 开始拖动某个点。 @param id 被拖动的点（可能是派生点，由控制器决定是否移动）。 */
    void pointDragStarted(ShapeId id);

    /** @brief 拖动中。 @param id 点 id；@param scenePos 新的场景坐标。 */
    void pointDragged(ShapeId id, const QPointF &scenePos);

    /** @brief 拖动结束。 */
    void pointDragFinished();

    /** @brief 选中集发生变化（画布已更新文档的选中集）。 @param ids 新的选中 id 列表。 */
    void selectionChanged(const QList<ShapeId> &ids);

    /** @brief 请求删除选中图元（Delete 键）。 */
    void deleteRequested();

    /** @brief 请求取消当前操作（Esc 键）。 */
    void escapeRequested();

    /** @brief 请求缩放。 @param factor 缩放倍数；@param scenePos 缩放锚点的场景坐标。 */
    void zoomRequested(double factor, const QPointF &scenePos);

    /** @brief 请求平移。 @param sceneDelta 场景坐标增量。 */
    void panRequested(const QPointF &sceneDelta);

    /**
     * @brief 开始"按下-拖动"式作图。
     * @param id 按下时命中的点 id；若按在空白处则为该处新建的自由点 id。
     * @details 有了它，作图工具（线段/圆/射线/直线/中点/距离/角度）既能"点两下"，
     *          也能"按住起点拖到终点松手"——后者是用户实际更顺手的操作方式。
     */
    void constructionDragStarted(ShapeId id);

    /** @brief 在"按下-拖动"式作图过程中松开鼠标。 @param scenePos 松开位置的场景坐标。 */
    void constructionDragFinished(const QPointF &scenePos);

    /** @brief 作图拖动被取消（例如拖回起点附近、或按了 Esc）。 */
    void constructionDragCancelled();

    // ----- 右键快捷菜单：全部是"以点击位置为起点"的语义化请求 -----

    /** @brief 在点击处新建一个自由点。 @param scenePos 场景坐标。 */
    void quickPointRequested(const QPointF &scenePos);

    /** @brief 以点击位置为起点开始画线段。 @param scenePos 场景坐标。 */
    void quickSegmentRequested(const QPointF &scenePos);

    /** @brief 以点击位置为圆心开始画圆。 @param scenePos 场景坐标。 */
    void quickCircleRequested(const QPointF &scenePos);

    /** @brief 以点击位置为起点测量到另一点的距离。 @param scenePos 场景坐标。 */
    void quickDistanceRequested(const QPointF &scenePos);

    /** @brief 右键菜单：删除当前选中的图元。 */
    void quickDeleteRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    /**
     * @brief 绘制网格（在变换前的屏幕坐标系下绘制，保证线宽恒为 1 像素）。
     * @param painter 画笔。
     */
    void drawGrid(QPainter *painter);

    /**
     * @brief 绘制坐标轴与刻度标签。
     * @param painter 画笔。
     */
    void drawAxes(QPainter *painter);

    /**
     * @brief 绘制网格与坐标轴上的数值标签。
     * @param painter 画笔。
     */
    void drawGridLabels(QPainter *painter);

    /**
     * @brief 绘制全部图元。
     * @param painter 画笔。
     */
    void drawShapes(QPainter *painter);

    /**
     * @brief 绘制单个图元。
     * @param painter 画笔。
     * @param shape 图元；为 nullptr 或无效时直接返回。
     */
    void drawShape(QPainter *painter, const Shape *shape);

    /**
     * @brief 绘制作图预览（橡皮筋、预览圆、已定顶点）。
     * @param painter 画笔。
     */
    void drawPreview(QPainter *painter);

    /**
     * @brief 绘制一个点（含选中高亮与悬停高亮）。
     * @param painter 画笔。
     * @param scenePos 点的场景坐标。
     * @param isSelected 是否被选中。
     * @param isHovered 是否被悬停。
     * @param isDerived 是否为派生点（中点/交点，用不同颜色以示"不可自由拖动"）。
     */
    void drawPointMarker(QPainter *painter,
                         const Vec2 &scenePos,
                         bool isSelected,
                         bool isHovered,
                         bool isDerived) const;

    /**
     * @brief 绘制一条直线族曲线（按可见区域裁剪后绘制）。
     * @param painter 画笔。
     * @param line 直线数据。
     * @param pen 画笔样式。
     */
    void drawLineData(QPainter *painter, const LineData &line, const QPen &pen) const;

    /**
     * @brief 绘制一段圆弧。
     * @param painter 画笔。
     * @param arc 弧数据。
     * @param pen 画笔样式。
     */
    void drawArcData(QPainter *painter, const ArcData &arc, const QPen &pen) const;

    /**
     * @brief 绘制一段文字的带底衬版本（标注与点名用，保证压在图形上仍可读）。
     * @param painter 画笔。
     * @param screenPos 文字锚点（屏幕坐标）。
     * @param text 文字内容。
     */
    void drawLabel(QPainter *painter, const QPointF &screenPos, const QString &text) const;

    /**
     * @brief 计算命中测试用的场景容差（由屏幕像素换算，保证不同缩放手感一致）。
     * @return 场景单位的容差。
     */
    double hitTolerance() const;

    /**
     * @brief 查找鼠标位置下的可拖动点。
     * @param scenePos 场景坐标。
     * @return 点 id；未命中返回 kInvalidShapeId。
     */
    ShapeId draggablePointAt(const Vec2 &scenePos) const;

    /**
     * @brief 更新悬停高亮并在必要时重绘。
     * @param scenePos 场景坐标。
     */
    void updateHover(const Vec2 &scenePos);

    /**
     * @brief 构造曲线与标注的画笔。
     * @param color 颜色。
     * @param widthF 线宽（像素）。
     * @param dash 是否为虚线。
     * @return 画笔对象。
     */
    static QPen makePen(const QColor &color, double widthF, bool dash = false);

    GeoDocument *m_document;      ///< 被显示的文档，不拥有。
    Camera m_camera;              ///< 视图变换。
    ToolId m_activeTool;          ///< 当前工具。

    QList<QPointF> m_previewPoints; ///< 预览：已确定的顶点（场景坐标）。
    QPointF m_previewHint;          ///< 预览：活动点（场景坐标）。
    bool m_hasPreviewHint;          ///< 是否显示活动点。
    bool m_previewAsCircle;         ///< 是否把"首点 → 活动点"解释为半径画预览圆。

    ShapeId m_hoveredId;          ///< 当前悬停的图元。
    ShapeId m_highlightId;        ///< 作图过程中锁定的输入图元（基准曲线等）。
    ShapeId m_dragCandidateId;    ///< 按下时命中的可拖动点。
    bool m_isDragging;            ///< 是否正在拖动。
    QPointF m_lastDragScreenPos;  ///< 上一次拖动事件的屏幕坐标。
    Vec2 m_lastHoverScenePos;     ///< 上一次发出坐标信号的场景位置（用于节流）。
    Vec2 m_constructPressScenePos;///< 作图按下时的场景坐标（松键时判断是否完成图形）。

    bool m_isPanning;             ///< 是否正在平移（中键或右键拖动）。
    Qt::MouseButton m_panButton;  ///< 触发本次平移的按键（中键 / 右键）。
    QPointF m_lastPanScreenPos;   ///< 上一次平移事件的屏幕坐标。
    QPointF m_rightPressScreenPos;///< 右键按下的屏幕坐标（用于区分"右键点击"与"右键拖动"）。
    bool m_rightDragMoved;        ///< 右键拖动是否已超过阈值（超过则不弹快捷菜单）。

    ShapeId m_constructionStartId;       ///< 作图工具"按下-拖动"的起点 id（仅当按在已有点上）。
    bool m_constructionArmed;            ///< 是否已进入可拖动的作图状态（按在空白处也算）。
    QPointF m_constructionStartScenePos; ///< 起点场景坐标（供预览与取消判断）。
    bool m_isConstructionDragging;       ///< 是否正在以"按下-拖动"方式作图。

    bool m_gridVisible;           ///< 网格开关。
    bool m_axesVisible;           ///< 坐标轴开关。
    bool m_labelsVisible;         ///< 标注开关。
    bool m_snapEnabled;           ///< 吸附开关。

    bool m_hasArcPreview;         ///< 是否显示"圆心 + 半径 + 弧上点"预览。
    QPointF m_arcPreviewCenter;   ///< 预览圆心。
    QPointF m_arcPreviewRim;      ///< 预览半径点。
    QPointF m_arcPreviewArcPoint; ///< 预览弧上点。
};

#endif // GEOBOARDCANVASWIDGET_H
