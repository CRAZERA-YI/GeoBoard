/**
 * @file    controller/ToolId.h
 * @brief   作图工具的枚举与文案：view 与 controller 共用的词汇表。
 * @details 工具枚举放在 controller 层，因为"某个工具需要几次点击、点完建什么图元"
 *          属于编排逻辑；view 只负责把工具栏选择翻译成这个枚举值。
 *
 *          不负责：工具的行为实现（见 MainController）、图元的构造（见 model/Shape.h）。
 */

#ifndef GEOBOARDTOOLID_H
#define GEOBOARDTOOLID_H

#include <QString>

/** @brief 作图工具。 */
enum class ToolId
{
    Select,            ///< 选择：选中图元、拖动点、拖动测量标注。
    Point,             ///< 独立点（自由点）。
    PointOnObject,     ///< 附着点：点在某条曲线上并受其约束。
    Segment,           ///< 线段（两点）。
    Ray,               ///< 射线（起点 + 方向点）。
    InfiniteLine,      ///< 直线（两点）。
    Circle,            ///< 圆（圆心 + 圆上一点）。
    Arc,               ///< 三点圆弧。
    ArcCenter,         ///< 圆弧（圆心 + 半径点 + 弧上点，从半径点逆时针扫到弧上点）。
    Polygon,           ///< 多边形（多次点击，双击结束）。
    MidPoint,          ///< 两点中点。
    Perpendicular,     ///< 垂线（先选基准曲线，再点通过点）。
    Parallel,          ///< 平行线（先选基准曲线，再点通过点）。
    AngleBisector,     ///< 角平分线（顶点、两边上各一点）。
    Intersection,      ///< 两曲线交点（依次点两条曲线）。
    MeasureDistance,   ///< 测量两点距离。
    MeasureLength,     ///< 测量线段长度（点选线段）。
    MeasureAngle,      ///< 测量角度（三点，第二次点击为顶点）。
    MeasureRadius,     ///< 测量圆半径（点选圆）。
    MeasureCircumference, ///< 测量圆周长（点选圆）。
    MeasureArea,       ///< 测量多边形面积（点选多边形）。
    MeasureCircleArea, ///< 测量圆面积（点选圆）。
    MeasureCoordinates ///< 标注点坐标（点选一个点）。
};

/**
 * @brief 返回工具的中文名称（用于工具栏按钮提示与状态栏）。
 * @param tool 工具。
 * @return 名称文本，调用方无需翻译（内部已用 tr）。
 */
QString toolName(ToolId tool);

/**
 * @brief 返回工具的简短操作说明（用于状态栏引导）。
 * @param tool 工具。
 * @return 例如"依次点击两点作线段"。
 */
QString toolHint(ToolId tool);

/**
 * @brief 返回工具需要的点击次数。
 * @param tool 工具。
 * @return 所需次数；Polygon 与 Select 返回 0 表示"次数不固定/不适用"。
 * @note  控制器用它判断"再点一次是否就该生成图元"，避免每种工具各写一遍计数比较。
 */
int toolClickCount(ToolId tool);

/**
 * @brief 判断某个工具能否用"按住起点拖到终点"来表达。
 * @param tool 工具。
 * @return 支持拖动作图返回 true。
 * @details 这是该判断的**唯一**定义处。过去它在 CanvasWidget.cpp 与
 *          MainController.cpp 里各写了一份，只靠注释提醒"必须保持一致"——
 *          结果新增"圆心式圆弧"时只改了一处，画布便永远走不到预览分支，
 *          用户看到的是"点了圆心之后毫无反应"（真实缺陷）。
 *          现在两处都调用本函数，结构上不可能再分叉。
 *
 *          垂线 / 平行线 / 交点 / 多边形需要先选图元或多次点击，不属于此列。
 */
inline bool isDragConstructionTool(ToolId tool)
{
    switch (tool) {
    case ToolId::Segment:
    case ToolId::Ray:
    case ToolId::InfiniteLine:
    case ToolId::Circle:
    case ToolId::MidPoint:
    case ToolId::MeasureDistance:
    case ToolId::MeasureAngle:
    // 圆心式圆弧的"拖动"含义是放下**弧上点**（第三次输入），前两次点击仍必须存在。
    case ToolId::ArcCenter:
        return true;
    default:
        return false;
    }
}

#endif // GEOBOARDTOOLID_H
