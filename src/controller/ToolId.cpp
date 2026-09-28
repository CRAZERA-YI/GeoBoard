/**
 * @file    controller/ToolId.cpp
 * @brief   工具枚举的文案与点击次数查询。
 * @details 把"工具叫什么、怎么用、需要几次点击"集中在这里，好处是：
 *          控制器不必为每个工具各写一遍计数与提示，新增工具时只改这一处。
 */

#include "ToolId.h"

#include <QCoreApplication>

QString toolName(ToolId tool)
{
    switch (tool) {
    case ToolId::Select:
        return QCoreApplication::translate("ToolId", "选择");
    case ToolId::Point:
        return QCoreApplication::translate("ToolId", "点");
    case ToolId::PointOnObject:
        return QCoreApplication::translate("ToolId", "对象上的点");
    case ToolId::Segment:
        return QCoreApplication::translate("ToolId", "线段");
    case ToolId::Ray:
        return QCoreApplication::translate("ToolId", "射线");
    case ToolId::InfiniteLine:
        return QCoreApplication::translate("ToolId", "直线");
    case ToolId::Circle:
        return QCoreApplication::translate("ToolId", "圆");
    case ToolId::Arc:
        return QCoreApplication::translate("ToolId", "圆弧");
    case ToolId::ArcCenter:
        return QCoreApplication::translate("ToolId", "圆弧(圆心)");
    case ToolId::Polygon:
        return QCoreApplication::translate("ToolId", "多边形");
    case ToolId::MidPoint:
        return QCoreApplication::translate("ToolId", "中点");
    case ToolId::Perpendicular:
        return QCoreApplication::translate("ToolId", "垂线");
    case ToolId::Parallel:
        return QCoreApplication::translate("ToolId", "平行线");
    case ToolId::AngleBisector:
        return QCoreApplication::translate("ToolId", "角平分线");
    case ToolId::Intersection:
        return QCoreApplication::translate("ToolId", "交点");
    case ToolId::MeasureDistance:
        return QCoreApplication::translate("ToolId", "距离");
    case ToolId::MeasureLength:
        return QCoreApplication::translate("ToolId", "线段长度");
    case ToolId::MeasureAngle:
        return QCoreApplication::translate("ToolId", "角度");
    case ToolId::MeasureRadius:
        return QCoreApplication::translate("ToolId", "圆半径");
    case ToolId::MeasureCircumference:
        return QCoreApplication::translate("ToolId", "圆周长");
    case ToolId::MeasureArea:
        return QCoreApplication::translate("ToolId", "多边形面积");
    case ToolId::MeasureCircleArea:
        return QCoreApplication::translate("ToolId", "圆面积");
    case ToolId::MeasureCoordinates:
        return QCoreApplication::translate("ToolId", "点坐标");
    }
    return QString();
}

QString toolHint(ToolId tool)
{
    switch (tool) {
    case ToolId::Select:
        return QCoreApplication::translate(
            "ToolId", "选择工具：点击图元选中（Ctrl 点击可多选），按住左键拖动点即可改变图形；Delete 删除选中");
    case ToolId::Point:
        return QCoreApplication::translate("ToolId", "点工具：点击一次创建一个自由点");
    case ToolId::PointOnObject:
        return QCoreApplication::translate(
            "ToolId", "对象上的点：点击一条曲线，在该曲线上创建一个受约束的点");
    case ToolId::Segment:
        return QCoreApplication::translate("ToolId", "线段：依次点击两点（点击已有点会复用它）");
    case ToolId::Ray:
        return QCoreApplication::translate("ToolId", "射线：先点击端点，再点击方向上的另一点");
    case ToolId::InfiniteLine:
        return QCoreApplication::translate("ToolId", "直线：依次点击两点");
    case ToolId::Circle:
        return QCoreApplication::translate("ToolId", "圆：先点击圆心，再点击圆上一点决定半径");
    case ToolId::Arc:
        return QCoreApplication::translate("ToolId", "圆弧：依次点击起点、弧上一点、终点");
    case ToolId::ArcCenter:
        return QCoreApplication::translate(
            "ToolId", "圆弧(圆心)：依次点击圆心、半径点、弧上一点；弧从半径点起逆时针扫到弧上点");
    case ToolId::Polygon:
        return QCoreApplication::translate("ToolId", "多边形：依次点击各顶点，双击结束绘制（至少 3 个顶点）");
    case ToolId::MidPoint:
        return QCoreApplication::translate("ToolId", "中点：依次点击两点，创建它们的中点");
    case ToolId::Perpendicular:
        return QCoreApplication::translate("ToolId", "垂线：先点击基准曲线，再点击垂线要经过的点");
    case ToolId::Parallel:
        return QCoreApplication::translate("ToolId", "平行线：先点击基准曲线，再点击平行线要经过的点");
    case ToolId::AngleBisector:
        return QCoreApplication::translate(
            "ToolId", "角平分线：依次点击角的顶点、第一条边上一点、第二条边上一点");
    case ToolId::Intersection:
        return QCoreApplication::translate("ToolId", "交点：依次点击两条曲线，创建它们的全部交点");
    case ToolId::MeasureDistance:
        return QCoreApplication::translate("ToolId", "距离：依次点击两点，创建距离标注");
    case ToolId::MeasureLength:
        return QCoreApplication::translate("ToolId", "线段长度：点击一条线段，创建长度标注");
    case ToolId::MeasureAngle:
        return QCoreApplication::translate("ToolId", "角度：依次点击角的一边上一点、顶点、另一边上一点");
    case ToolId::MeasureRadius:
        return QCoreApplication::translate("ToolId", "圆半径：点击一个圆，创建半径标注");
    case ToolId::MeasureCircumference:
        return QCoreApplication::translate("ToolId", "圆周长：点击一个圆，创建周长标注");
    case ToolId::MeasureArea:
        return QCoreApplication::translate("ToolId", "多边形面积：点击一个多边形，创建面积标注");
    case ToolId::MeasureCircleArea:
        return QCoreApplication::translate("ToolId", "圆面积：点击一个圆，创建面积标注");
    case ToolId::MeasureCoordinates:
        return QCoreApplication::translate("ToolId", "点坐标：点击一个点，创建它的坐标标注");
    }
    return QString();
}

int toolClickCount(ToolId tool)
{
    switch (tool) {
    case ToolId::Select:
    case ToolId::Polygon:
        return 0; // 次数不固定：选择工具不靠点击计数，多边形由双击结束
    case ToolId::Point:
    case ToolId::PointOnObject:
    case ToolId::MeasureLength:
    case ToolId::MeasureRadius:
    case ToolId::MeasureCircumference:
    case ToolId::MeasureArea:
    case ToolId::MeasureCircleArea:
    case ToolId::MeasureCoordinates:
        return 1;
    case ToolId::Segment:
    case ToolId::Ray:
    case ToolId::InfiniteLine:
    case ToolId::Circle:
    case ToolId::MidPoint:
    case ToolId::Perpendicular:
    case ToolId::Parallel:
    case ToolId::MeasureDistance:
        return 2;
    case ToolId::Arc:
    case ToolId::AngleBisector:
    case ToolId::MeasureAngle:
    case ToolId::ArcCenter:
        return 3;
    case ToolId::Intersection:
        return 2; // 依次点两条曲线
    }
    return 0;
}
