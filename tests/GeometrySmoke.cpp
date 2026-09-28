/**
 * @file    tests/geometry_smoke.cpp
 * @brief   几何引擎自测与重算性能基准。
 * @details 这是一个**无 GUI** 的控制台程序：只链接 model 层的几何代码，
 *          不创建 QApplication，也不包含任何界面代码。
 *
 *          它能编译并运行通过，本身就是两条结论的证据：
 *          1. model 层没有混入界面依赖（否则这里编译不过）；
 *          2. 动态几何的核心语义（依赖重算、交点、退化处理）符合设计文档。
 *
 *          运行方式见 docs/test.md；全部用例通过时进程返回 0，否则返回 1。
 */

#include "model/Camera.h"
#include "model/GeoDocument.h"
#include "model/GeometryMath.h"
#include "model/Shape.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QList>
#include <QString>
#include <QStringList>

#include <cmath>
#include <limits>
#include <cstdio>

/**
 * @brief 测试专用外壳：借 GeoDocument 的 friend 声明读取内部索引。
 * @details 生产代码不得访问文档内部；这里唯一的用途是验证
 *          "父 id < 自身 id"这一拓扑序不变式——它是增量重算正确性的前提，
 *          必须被测试直接证实，而不是靠"应该没问题"。
 */
class GeometrySmokeTest
{
public:
    /**
     * @brief 校验文档内部索引是否满足拓扑序不变式。
     * @param document 待校验的文档。
     * @param violationId 输出参数：第一个违反不变式的图元 id（若存在）。
     * @param parentId 输出参数：该图元违反约定的父 id（若存在）。
     * @return 全部满足时返回 true。
     */
    static bool verifyTopologicalOrder(const GeoDocument &document,
                                       ShapeId *violationId,
                                       ShapeId *parentId)
    {
        for (QHash<ShapeId, Shape *>::const_iterator it = document.m_shapes.constBegin();
             it != document.m_shapes.constEnd(); ++it) {
            const QList<ShapeId> parents = it.value()->parents();
            for (int i = 0; i < parents.size(); ++i) {
                if (parents.at(i) >= it.key()) {
                    if (violationId != nullptr) {
                        *violationId = it.key();
                    }
                    if (parentId != nullptr) {
                        *parentId = parents.at(i);
                    }
                    return false;
                }
            }
        }
        return true;
    }
};

namespace {

/** @brief 输出文本（Windows 控制台已设为 UTF-8，见 docs/test.md）。 */
void print(const QString &text)
{
    const QByteArray utf8 = text.toUtf8();
    std::fputs(utf8.constData(), stdout);
    std::fputs("\n", stdout);
}

/** @brief 断言两数在容差内相等。 */
bool nearlyEqual(double a, double b, double tolerance = 1e-9)
{
    return std::fabs(a - b) <= tolerance;
}

/** @brief 断言两向量在容差内相等。 */
bool nearlyEqual(const Vec2 &a, const Vec2 &b, double tolerance = 1e-9)
{
    return a.isCloseTo(b, tolerance);
}

/** @brief 判断列表中是否包含某点（容差比较）。 */
bool containsPoint(const QList<Vec2> &points, const Vec2 &expected, double tolerance = 1e-6)
{
    for (int i = 0; i < points.size(); ++i) {
        if (nearlyEqual(points.at(i), expected, tolerance)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 极简测试运行器：统计通过/失败并输出可读结果。
 */
class TestRunner
{
public:
    /**
     * @brief 构造运行器。
     * @param suiteName 用例集名称。
     */
    explicit TestRunner(const QString &suiteName)
        : m_suiteName(suiteName)
        , m_passed(0)
        , m_failed(0)
    {
    }

    /**
     * @brief 记录一条断言结果。
     * @param condition 断言是否成立。
     * @param description 断言描述。
     * @param detail 失败时的补充信息。
     */
    void check(bool condition, const QString &description, const QString &detail = QString())
    {
        if (condition) {
            ++m_passed;
            return;
        }
        ++m_failed;
        print(QStringLiteral("[FAIL] %1").arg(description));
        if (!detail.isEmpty()) {
            print(QStringLiteral("       %1").arg(detail));
        }
    }

    /**
     * @brief 输出用例集结论。
     * @return 全部通过时返回 0，否则返回 1。
     */
    int report() const
    {
        print(QStringLiteral("---- %1: 通过 %2 / 失败 %3 ----")
                  .arg(m_suiteName)
                  .arg(m_passed)
                  .arg(m_failed));
        return m_failed == 0 ? 0 : 1;
    }

private:
    QString m_suiteName; ///< 用例集名称。
    int m_passed;        ///< 通过数。
    int m_failed;        ///< 失败数。
};

// ---------------------------------------------------------------------------
// 构造辅助
// ---------------------------------------------------------------------------

/**
 * @brief 往文档里加一个自由点。
 * @param document 目标文档。
 * @param x 横坐标。
 * @param y 纵坐标。
 * @return 新点的 id。
 */
ShapeId addFreePoint(GeoDocument *document, double x, double y)
{
    const ShapeId id = document->allocateShapeId();
    document->addShape(new PointShape(id, PointKind::Free, Vec2(x, y)));
    return id;
}

/**
 * @brief 往文档里加一条线（线段/射线/直线）。
 * @param document 目标文档。
 * @param kind 有界方式。
 * @param firstPointId 起点。
 * @param secondPointId 第二点。
 * @return 新线的 id。
 */
ShapeId addLine(GeoDocument *document, LineKind kind, ShapeId firstPointId, ShapeId secondPointId)
{
    const ShapeId id = document->allocateShapeId();
    document->addShape(new LineShape(id, kind, firstPointId, secondPointId));
    return id;
}

/**
 * @brief 往文档里加一个圆。
 * @param document 目标文档。
 * @param centerPointId 圆心点。
 * @param rimPointId 圆上点。
 * @return 新圆的 id。
 */
ShapeId addCircle(GeoDocument *document, ShapeId centerPointId, ShapeId rimPointId)
{
    const ShapeId id = document->allocateShapeId();
    document->addShape(new CircleShape(id, centerPointId, rimPointId));
    return id;
}

/**
 * @brief 按模板 .pro/.pri 的 include 路径取点坐标。
 * @param document 文档。
 * @param id 点 id。
 * @return 点坐标；不存在时返回 (NaN, NaN)。
 */
Vec2 positionOf(GeoDocument *document, ShapeId id)
{
    const PointShape *point = document->point(id);
    if (point == nullptr) {
        return Vec2(std::nan(""), std::nan(""));
    }
    return point->position();
}

} // namespace

// ---------------------------------------------------------------------------
// 用例集：数学工具
// ---------------------------------------------------------------------------

/** @brief 验证 GeometryMath 的求解结果。 */
int runMathTests()
{
    TestRunner runner(QStringLiteral("GeometryMath"));

    // 交点：直线 × 圆，两解
    {
        QList<Vec2> hits;
        const int count = GeoMath::intersectLineCircle(Vec2(0.0, 0.0), Vec2(1.0, 0.0),
                                                       Vec2(0.0, 0.0), 5.0, &hits);
        runner.check(count == 2, QStringLiteral("直线与圆应交于两点"),
                     QStringLiteral("实际得到 %1 个").arg(count));
        runner.check(containsPoint(hits, Vec2(5.0, 0.0)) && containsPoint(hits, Vec2(-5.0, 0.0)),
                     QStringLiteral("直线与圆的交点应为 (±5, 0)"));
    }

    // 相切容易被数值误差漏判，单独覆盖
    {
        QList<Vec2> hits;
        const int count = GeoMath::intersectLineCircle(Vec2(0.0, 5.0), Vec2(1.0, 0.0),
                                                       Vec2(0.0, 0.0), 5.0, &hits);
        runner.check(count == 1, QStringLiteral("直线与圆相切应得到唯一交点"),
                     QStringLiteral("实际得到 %1 个").arg(count));
    }

    // 圆 × 圆：两解
    {
        QList<Vec2> hits;
        const int count = GeoMath::intersectCircleCircle(Vec2(0.0, 0.0), 5.0,
                                                         Vec2(6.0, 0.0), 5.0, &hits);
        runner.check(count == 2, QStringLiteral("两圆应交于两点"),
                     QStringLiteral("实际得到 %1 个").arg(count));
    }

    // 同心圆重合：不做为约束
    {
        QList<Vec2> hits;
        const int count = GeoMath::intersectCircleCircle(Vec2(0.0, 0.0), 5.0,
                                                         Vec2(0.0, 0.0), 5.0, &hits);
        runner.check(count == 0, QStringLiteral("重合圆不应产生交点（无穷多解不作为约束）"));
    }

    // 外接圆
    {
        Vec2 center;
        double radius = 0.0;
        const bool ok = GeoMath::circumcenter(Vec2(0.0, 0.0), Vec2(10.0, 0.0), Vec2(5.0, 5.0), &center)
            && GeoMath::circumradius(Vec2(0.0, 0.0), Vec2(10.0, 0.0), Vec2(5.0, 5.0), &radius);
        runner.check(ok && nearlyEqual(center, Vec2(5.0, 0.0), 1e-9) && nearlyEqual(radius, 5.0, 1e-9),
                     QStringLiteral("三点外接圆圆心应为 (5,0)、半径 5"),
                     QStringLiteral("圆心 (%1,%2) 半径 %3").arg(center.x).arg(center.y).arg(radius));
    }

    // 共线三点没有外接圆
    {
        Vec2 center;
        const bool ok = GeoMath::circumcenter(Vec2(0.0, 0.0), Vec2(1.0, 1.0), Vec2(2.0, 2.0), &center);
        runner.check(!ok, QStringLiteral("共线三点必须判定为无外接圆"));
    }

    // 角平分线方向
    {
        Vec2 direction;
        const bool ok = GeoMath::angleBisectorDirection(Vec2(0.0, 0.0), Vec2(1.0, 0.0),
                                                        Vec2(0.0, 1.0), &direction);
        const Vec2 expected = Vec2(1.0, 1.0).normalized();
        runner.check(ok && nearlyEqual(direction, expected, 1e-9),
                     QStringLiteral("直角的角平分线方向应为 (√2/2, √2/2)"));
    }

    // 面积
    {
        QList<Vec2> square;
        square << Vec2(0.0, 0.0) << Vec2(4.0, 0.0) << Vec2(4.0, 3.0) << Vec2(0.0, 3.0);
        runner.check(nearlyEqual(GeoMath::polygonArea(square), 12.0, 1e-9),
                     QStringLiteral("4×3 矩形面积应为 12"));
    }

    // 投影：线段外点应投到端点
    {
        const Vec2 projected = GeoMath::projectPointOnSegment(Vec2(10.0, 5.0), Vec2(0.0, 0.0), Vec2(5.0, 0.0));
        runner.check(nearlyEqual(projected, Vec2(5.0, 0.0), 1e-9),
                     QStringLiteral("线段外点的投影应落在线段端点"));
    }

    return runner.report();
}

// ---------------------------------------------------------------------------
// 用例集：依赖重算
// ---------------------------------------------------------------------------

/** @brief 验证"拖动点 → 依赖图形自动跟随"这一核心语义。 */
int runDependencyTests()
{
    TestRunner runner(QStringLiteral("依赖重算"));

    GeoDocument document;
    const ShapeId centerId = addFreePoint(&document, 0.0, 0.0);
    const ShapeId rimId = addFreePoint(&document, 3.0, 0.0);
    const ShapeId circleId = addCircle(&document, centerId, rimId);

    const ShapeId segAId = addFreePoint(&document, 0.0, 0.0);
    const ShapeId segBId = addFreePoint(&document, 8.0, 0.0);
    const ShapeId segmentId = addLine(&document, LineKind::Segment, segAId, segBId);

    // 线段在 x 轴上、从原点向右延伸，圆以原点为心、半径 3。
    // 注意：线段只覆盖直径的一半，因此**只有 1 个交点**（x 轴负半轴那一半不在线段上）。
    // 这条用例曾经被我写成"2 个交点"，是靠实测（候选点 (-3,0) 的参数为 -0.375）
    // 才纠正过来的——几何断言必须先算清楚再写进测试。
    QList<Vec2> hits;
    const int intersectionCount = document.curveIntersections(circleId, segmentId, &hits);
    runner.check(intersectionCount == 1 && containsPoint(hits, Vec2(3.0, 0.0), 1e-6),
                 QStringLiteral("半径 3 的圆与 [0,8] 线段应只有 1 个交点 (3,0)"),
                 QStringLiteral("实际 %1 个").arg(intersectionCount));

    // 把线段移到 y=2.5 并让它**横跨**圆（左端到 x=-5，否则线段只覆盖圆的右侧，
    // 仍然只有 1 个交点——这是这条用例第二次被我写错的地方）
    document.movePoint(segAId, Vec2(-5.0, 2.5));
    document.movePoint(segBId, Vec2(8.0, 2.5));
    hits.clear();
    const int twoCount = document.curveIntersections(circleId, segmentId, &hits);
    runner.check(twoCount == 2, QStringLiteral("横跨圆的线段（y=2.5，x 从 -5 到 8）应有 2 个交点"),
                 QStringLiteral("实际 %1 个").arg(twoCount));

    // 动态性：把圆上点拖到半径 10，线段整条落入圆内，交点应消失
    document.movePoint(rimId, Vec2(10.0, 0.0));
    const CircleData &circle = static_cast<const CircleShape *>(document.shape(circleId))->circleData();
    runner.check(nearlyEqual(circle.radius, 10.0, 1e-9),
                 QStringLiteral("拖动圆上点后半径应变为 10"),
                 QStringLiteral("实际半径 %1").arg(circle.radius));

    hits.clear();
    const int afterCount = document.curveIntersections(circleId, segmentId, &hits);
    runner.check(afterCount == 0,
                 QStringLiteral("半径 10 时该线段（最远点距圆心约 9.4）整条落入圆内，应无交点"),
                 QStringLiteral("实际 %1 个").arg(afterCount));

    // 交点图元：应跟随父曲线移动
    const ShapeId intersectionId = document.allocateShapeId();
    document.addShape(new IntersectionPointShape(intersectionId, circleId, segmentId, 0));
    const IntersectionPointShape *intersection =
        static_cast<const IntersectionPointShape *>(document.shape(intersectionId));
    runner.check(!intersection->isValid(),
                 QStringLiteral("无交点时交点图元应无效"));

    // 让圆的半径回到 3（线段在 y=2.5 上横跨圆）→ 交点应自动恢复
    document.movePoint(rimId, Vec2(3.0, 0.0));
    runner.check(intersection->isValid(), QStringLiteral("半径变回 3 后交点应自动恢复有效"));
    runner.check(document.shape(intersectionId) != nullptr,
                 QStringLiteral("交点失效期间图元应始终保留在文档中（不删除）"));

    return runner.report();
}

// ---------------------------------------------------------------------------
// 用例集：角度测量（用户反馈"角度不能用"的回归点）
// ---------------------------------------------------------------------------

/**
 * @brief 验证角度测量的取值、退化判定与提示文本。
 * @details 用户报告"角度功能不能正常使用"。排查结论是**两个原因叠加**：
 *          1. 几何上：当第三个点与角的顶点重合时，角的两条边有一条退化为零向量，
 *             角不存在，标注显示"—"。这不是缺陷而是正确的退化处理；
 *          2. 交互上（真正的缺陷）：点击顶点附近时，第三个点会被**吸附到顶点**，
 *             于是每次都稳定地落进上面那个退化分支，界面上没有任何提示，
 *             用户看到的就是"点了没反应"。修法是作图取点时代入"已用过的点"作为
 *             排除项（`MainController::acquirePoint` 的 excludedIds）。
 *          本用例把第 1 条的几何契约固定下来，防止将来有人"顺手"把它改成返回 0°——
 *          返回 0° 会让退化与真实的 0° 角无法区分。
 */
int runAngleTests()
{
    TestRunner runner(QStringLiteral("角度测量"));

    GeoDocument document;
    const ShapeId vertexId = addFreePoint(&document, 0.0, 0.0);
    const ShapeId firstId = addFreePoint(&document, 5.0, 0.0);
    const ShapeId secondId = addFreePoint(&document, 0.0, 5.0);

    // 输入顺序约定：第一个点 → 顶点 → 第二个点，顶点在中间
    QList<ShapeId> inputs;
    inputs << firstId << vertexId << secondId;
    const ShapeId angleId = document.allocateShapeId();
    document.addShape(new MeasurementShape(angleId, MeasurementKind::Angle, inputs,
                                           kInvalidShapeId, Vec2(0.0, 0.0)));

    const MeasurementShape *angle = static_cast<const MeasurementShape *>(document.shape(angleId));
    runner.check(angle->isValid(), QStringLiteral("正常三点角应有效"));
    runner.check(nearlyEqual(angle->value(), GeoMath::kPi / 2.0, 1e-9),
                 QStringLiteral("(5,0)-(0,0)-(0,5) 的角应为 90°"),
                 QStringLiteral("实际 %1°").arg(angle->value() * 180.0 / GeoMath::kPi));
    runner.check(angle->displayText() == QStringLiteral("90.0°"),
                 QStringLiteral("角度标注应显示 90.0°"),
                 QStringLiteral("实际 %1").arg(angle->displayText()));

    // 拖动一条边 → 角度必须跟着变（这是"动态"的核心）
    document.movePoint(secondId, Vec2(5.0, 5.0));
    const double expected = 45.0 * GeoMath::kPi / 180.0;
    runner.check(nearlyEqual(angle->value(), expected, 1e-9),
                 QStringLiteral("一条边转到 (5,5) 后角应为 45°"),
                 QStringLiteral("实际 %1°").arg(angle->value() * 180.0 / GeoMath::kPi));

    // 三点共线且两条边同向：夹角是 0°，这是**合法**取值，不能判为无效
    // （注意：这里曾把期望写成 180°，是测试算错了——顶点在中间时，
    //   两个边向量 (5,0) 与 (10,0) 同向，夹角为 0°）
    document.movePoint(secondId, Vec2(10.0, 0.0));
    runner.check(angle->isValid() && nearlyEqual(angle->value(), 0.0, 1e-9),
                 QStringLiteral("三点共线且两边同向时应为 0°（合法角度，不得判为无效）"),
                 QStringLiteral("有效=%1 值=%2°").arg(angle->isValid())
                     .arg(angle->value() * 180.0 / GeoMath::kPi));

    // 真正的平角：顶点仍在中间，但两条边反向 → 180°
    document.movePoint(firstId, Vec2(5.0, 0.0));
    document.movePoint(secondId, Vec2(-5.0, 0.0));
    runner.check(angle->isValid() && nearlyEqual(angle->value(), GeoMath::kPi, 1e-9),
                 QStringLiteral("两边反向时应为 180°（平角，合法取值）"),
                 QStringLiteral("有效=%1 值=%2°").arg(angle->isValid())
                     .arg(angle->value() * 180.0 / GeoMath::kPi));

    // 复位成直角，后面的退化用例从这里继续
    document.movePoint(secondId, Vec2(0.0, 5.0));

    // 第三个点与顶点重合 → 角不存在。这正是"点顶点附近没反应"时落进的退化分支，
    // 修好吸附后用户不会再走到这里，但几何契约必须保持不变。
    document.movePoint(secondId, Vec2(0.0, 0.0));
    runner.check(!angle->isValid(),
                 QStringLiteral("第三点与顶点重合时角不存在，应判为无效（显示占位符）"));
    runner.check(angle->displayText() == QStringLiteral("—"),
                 QStringLiteral("无效角度标注应显示占位符「—」而不是 0°"),
                 QStringLiteral("实际 %1").arg(angle->displayText()));
    runner.check(document.shape(angleId) != nullptr,
                 QStringLiteral("角度失效后标注应保留在文档中（拖开即自动恢复）"));

    // 拖开 → 自动恢复
    document.movePoint(secondId, Vec2(0.0, 5.0));
    runner.check(angle->isValid(), QStringLiteral("拖开后角度标注应自动恢复有效"));

    // 顶点与第一条边上的点重合，同样应判无效（另一条边的退化路径）
    document.movePoint(firstId, Vec2(0.0, 0.0));
    runner.check(!angle->isValid(),
                 QStringLiteral("第一条边上的点与顶点重合时角同样不存在"));

    // 恢复：180° 用例之后 firstId 应当回到 (5,0)，否则上面的"恢复"检查会误判；
    // 这里显式确认一下，避免用例之间互相污染
    document.movePoint(firstId, Vec2(5.0, 0.0));
    runner.check(angle->isValid(), QStringLiteral("恢复第一条边后角应重新有效"));

    return runner.report();
}

// ---------------------------------------------------------------------------
// 用例集：中点、垂线、平行线与附着点
// ---------------------------------------------------------------------------

/** @brief 验证派生图元的几何正确性。 */
int runDerivedShapeTests()
{
    TestRunner runner(QStringLiteral("派生图元"));

    GeoDocument document;
    const ShapeId aId = addFreePoint(&document, 0.0, 0.0);
    const ShapeId bId = addFreePoint(&document, 4.0, 4.0);
    const ShapeId segmentId = addLine(&document, LineKind::Segment, aId, bId);

    // 中点
    const ShapeId midId = document.allocateShapeId();
    document.addShape(new MidPointShape(midId, aId, bId));
    const MidPointShape *mid = static_cast<const MidPointShape *>(document.shape(midId));
    runner.check(nearlyEqual(mid->position(), Vec2(2.0, 2.0), 1e-9),
                 QStringLiteral("(0,0) 与 (4,4) 的中点应为 (2,2)"));

    // 垂线：过 (0,4) 且垂直于方向 (1,1) → 方向应为 (-1,1)
    const ShapeId throughId = addFreePoint(&document, 0.0, 4.0);
    const ShapeId perpendicularId = document.allocateShapeId();
    document.addShape(new LineShape(perpendicularId, ShapeType::PerpendicularLine, segmentId, throughId));
    const LineShape *perpendicular = static_cast<const LineShape *>(document.shape(perpendicularId));
    runner.check(perpendicular->isValid(), QStringLiteral("垂线应有效"));
    {
        const Vec2 expected = Vec2(-1.0, 1.0).normalized();
        const Vec2 actual = perpendicular->lineData().direction.normalized();
        runner.check(nearlyEqual(actual, expected, 1e-9) || nearlyEqual(actual, -expected, 1e-9),
                     QStringLiteral("垂直于方向 (1,1) 的直线方向应为 (±1,∓1)"));
    }

    // 平行线：方向应与基准一致
    const ShapeId parallelId = document.allocateShapeId();
    document.addShape(new LineShape(parallelId, ShapeType::ParallelLine, segmentId, throughId));
    const LineShape *parallel = static_cast<const LineShape *>(document.shape(parallelId));
    {
        const Vec2 base = static_cast<const LineShape *>(document.shape(segmentId))->lineData().direction.normalized();
        const Vec2 actual = parallel->lineData().direction.normalized();
        runner.check(nearlyEqual(actual, base, 1e-9), QStringLiteral("平行线方向应与基准曲线一致"));
    }

    // ---- 圆弧的两种构造方式 ----
    {
        // ① 圆心 + 两点：圆心 (0,0)、半径点 (3,0)、弧上点 (0,3)
        const ShapeId arcCenterId = addFreePoint(&document, 0.0, 0.0);
        const ShapeId arcRadiusId = addFreePoint(&document, 3.0, 0.0);
        const ShapeId arcOnCurveId = addFreePoint(&document, 0.0, 3.0);
        const ShapeId centerArcId = document.allocateShapeId();
        document.addShape(new ArcShape(centerArcId, arcCenterId, arcRadiusId, arcOnCurveId,
                                       ArcKind::CenterTwoPoint));
        const ArcShape *centerArc = static_cast<const ArcShape *>(document.shape(centerArcId));
        runner.check(centerArc->kind() == ArcKind::CenterTwoPoint,
                     QStringLiteral("圆心式弧应记住自己的构造方式"));
        runner.check(centerArc->isValid(), QStringLiteral("圆心式弧应有效"));
        runner.check(nearlyEqual(centerArc->arcData().center, Vec2(0.0, 0.0), 1e-9)
                         && nearlyEqual(centerArc->arcData().radius, 3.0, 1e-9),
                     QStringLiteral("圆心式弧的圆心应为 (0,0)、半径应为 3"),
                     QStringLiteral("圆心 (%1,%2) 半径 %3")
                         .arg(centerArc->arcData().center.x).arg(centerArc->arcData().center.y)
                         .arg(centerArc->arcData().radius));
        runner.check(nearlyEqual(centerArc->arcData().sweep(), GeoMath::kPi / 2.0, 1e-9),
                     QStringLiteral("从 (3,0) 逆时针扫到 (0,3) 应为 90°"),
                     QStringLiteral("实际 %1°").arg(centerArc->arcData().sweep() * 180.0 / GeoMath::kPi));

        // 拖动半径点 → 半径必须跟着变，而圆心不动（这正是这种构造方式的价值）
        document.movePoint(arcRadiusId, Vec2(5.0, 0.0));
        runner.check(nearlyEqual(centerArc->arcData().radius, 5.0, 1e-9)
                         && nearlyEqual(centerArc->arcData().center, Vec2(0.0, 0.0), 1e-9),
                     QStringLiteral("拖动半径点后半径变为 5、圆心保持不动"),
                     QStringLiteral("半径 %1").arg(centerArc->arcData().radius));

        // 半径点与圆心重合 → 退化，但图元保留
        document.movePoint(arcRadiusId, Vec2(0.0, 0.0));
        runner.check(!centerArc->isValid(),
                     QStringLiteral("半径点与圆心重合时弧应失效（而不是产生 NaN）"));
        runner.check(document.shape(centerArcId) != nullptr,
                     QStringLiteral("失效的弧应保留在文档中，拖开即恢复"));
        document.movePoint(arcRadiusId, Vec2(3.0, 0.0));
        runner.check(centerArc->isValid(), QStringLiteral("拖开后圆心式弧应自动恢复有效"));

        // ② 三点弧仍然按外接圆工作，且 kind 保持 ThreePoint（不能被新构造方式污染）
        const ShapeId threeArcId = document.allocateShapeId();
        document.addShape(new ArcShape(threeArcId, arcCenterId, arcOnCurveId, arcRadiusId));
        const ArcShape *threeArc = static_cast<const ArcShape *>(document.shape(threeArcId));
        runner.check(threeArc->kind() == ArcKind::ThreePoint,
                     QStringLiteral("三点弧的 kind 应为 ThreePoint"));
        runner.check(threeArc->isValid(), QStringLiteral("三点弧应有效"));
        // (0,0)-(0,3)-(3,0) 是直角边为 3 的直角三角形，斜边为直径 3√2，
        // 因此外接圆半径是 3√2/2 ≈ 2.1213（这里曾把期望写成 3，是测试算错了）
        runner.check(nearlyEqual(threeArc->arcData().radius, 3.0 * std::sqrt(2.0) / 2.0, 1e-9),
                     QStringLiteral("三点弧 (0,0)-(0,3)-(3,0) 的外接圆半径应为 3√2/2"),
                     QStringLiteral("实际 %1").arg(threeArc->arcData().radius));
        runner.check(nearlyEqual(threeArc->arcData().center, Vec2(1.5, 1.5), 1e-9),
                     QStringLiteral("该三点弧的圆心应为 (1.5, 1.5)"),
                     QStringLiteral("实际 (%1,%2)")
                         .arg(threeArc->arcData().center.x).arg(threeArc->arcData().center.y));
    }

    // 附着点：应被投影到线段上
    const ShapeId attachedId = document.allocateShapeId();
    PointShape *attached = new PointShape(attachedId, PointKind::OnObject, Vec2(10.0, -5.0));
    attached->setHostId(segmentId);
    document.addShape(attached);
    attached = document.point(attachedId);
    runner.check(attached->isValid(), QStringLiteral("附着点应有效"));
    {
        // 线段是 (0,0)-(4,4)。点 (10,-5) 的投影参数 t = (10·4 + (-5)·4)/32 = 0.625，
        // 落在 [0,1] 内，因此投影点应是 (2.5, 2.5)——**不是**端点
        runner.check(nearlyEqual(attached->position(), Vec2(2.5, 2.5), 1e-6),
                     QStringLiteral("线段内投影应落在垂足上 (2.5,2.5)"),
                     QStringLiteral("实际位置 (%1,%2)").arg(attached->position().x).arg(attached->position().y));
    }

    // 投影超出线段范围时必须夹到端点，否则附着点会跑到线段的延长线上
    document.movePoint(attachedId, Vec2(-10.0, -10.0));
    runner.check(nearlyEqual(attached->position(), Vec2(0.0, 0.0), 1e-6),
                 QStringLiteral("投影参数为负时应夹到线段起点 (0,0)"),
                 QStringLiteral("实际位置 (%1,%2)").arg(attached->position().x).arg(attached->position().y));
    document.movePoint(attachedId, Vec2(20.0, 20.0));
    runner.check(nearlyEqual(attached->position(), Vec2(4.0, 4.0), 1e-6),
                 QStringLiteral("投影参数大于 1 时应夹到线段终点 (4,4)"),
                 QStringLiteral("实际位置 (%1,%2)").arg(attached->position().x).arg(attached->position().y));

    // 附着点在线段上滑动：拖到线段内部应停在垂足上
    document.movePoint(attachedId, Vec2(1.0, 1.0));
    runner.check(nearlyEqual(attached->position(), Vec2(1.0, 1.0), 1e-6),
                 QStringLiteral("附着点在线段上拖动后应留在线上"));

    // 测量：半径、周长、面积都要跟随圆的半径变化
    {
        const ShapeId centerId = addFreePoint(&document, 0.0, 0.0);
        const ShapeId rimId = addFreePoint(&document, 3.0, 0.0);
        const ShapeId circleId = addCircle(&document, centerId, rimId);

        const ShapeId radiusId = document.allocateShapeId();
        document.addShape(new MeasurementShape(radiusId, MeasurementKind::Radius,
                                               QList<ShapeId>(), circleId, Vec2(0.0, 0.0)));
        const ShapeId circumferenceId = document.allocateShapeId();
        document.addShape(new MeasurementShape(circumferenceId, MeasurementKind::Circumference,
                                               QList<ShapeId>(), circleId, Vec2(0.0, 0.0)));
        const ShapeId areaId = document.allocateShapeId();
        document.addShape(new MeasurementShape(areaId, MeasurementKind::Area,
                                               QList<ShapeId>(), circleId, Vec2(0.0, 0.0)));

        const MeasurementShape *radiusMeasure =
            static_cast<const MeasurementShape *>(document.shape(radiusId));
        const MeasurementShape *circumferenceMeasure =
            static_cast<const MeasurementShape *>(document.shape(circumferenceId));
        const MeasurementShape *areaMeasure =
            static_cast<const MeasurementShape *>(document.shape(areaId));

        runner.check(nearlyEqual(radiusMeasure->value(), 3.0, 1e-9),
                     QStringLiteral("半径测量应为 3"),
                     QStringLiteral("实际 %1").arg(radiusMeasure->value()));
        runner.check(nearlyEqual(circumferenceMeasure->value(), 2.0 * GeoMath::kPi * 3.0, 1e-9),
                     QStringLiteral("周长测量应为 2πr"),
                     QStringLiteral("实际 %1").arg(circumferenceMeasure->value()));
        runner.check(nearlyEqual(areaMeasure->value(), GeoMath::kPi * 9.0, 1e-9),
                     QStringLiteral("圆面积测量应为 πr²"),
                     QStringLiteral("实际 %1").arg(areaMeasure->value()));

        // 动态性：半径改成 5，三个测量值都必须跟着变
        document.movePoint(rimId, Vec2(5.0, 0.0));
        runner.check(nearlyEqual(radiusMeasure->value(), 5.0, 1e-9)
                         && nearlyEqual(circumferenceMeasure->value(), 2.0 * GeoMath::kPi * 5.0, 1e-9)
                         && nearlyEqual(areaMeasure->value(), GeoMath::kPi * 25.0, 1e-9),
                     QStringLiteral("半径改为 5 后，半径/周长/面积三个标注都应更新"));

        // 坐标标注
        const ShapeId coordinateId = document.allocateShapeId();
        QList<ShapeId> coordinateInputs;
        coordinateInputs << rimId;
        document.addShape(new MeasurementShape(coordinateId, MeasurementKind::PointCoordinates,
                                               coordinateInputs, kInvalidShapeId, Vec2(0.0, 0.0)));
        const MeasurementShape *coordinateMeasure =
            static_cast<const MeasurementShape *>(document.shape(coordinateId));
        runner.check(coordinateMeasure->isValid()
                         && coordinateMeasure->displayText().contains(QLatin1Char('5')),
                     QStringLiteral("坐标标注应显示当前坐标 (5.00, 0.00)"),
                     QStringLiteral("实际文本 %1").arg(coordinateMeasure->displayText()));
    }

    return runner.report();
}

// ---------------------------------------------------------------------------
// 用例集：退化处理
// ---------------------------------------------------------------------------

/** @brief 验证各种退化情形都被标记为无效而不是崩溃或产生 NaN。 */
int runDegenerateTests()
{
    TestRunner runner(QStringLiteral("退化处理"));

    GeoDocument document;
    const ShapeId aId = addFreePoint(&document, 0.0, 0.0);
    const ShapeId bId = addFreePoint(&document, 5.0, 0.0);
    const ShapeId cId = addFreePoint(&document, 2.0, 3.0);

    // 三点弧
    const ShapeId arcId = document.allocateShapeId();
    document.addShape(new ArcShape(arcId, aId, cId, bId));
    const ArcShape *arc = static_cast<const ArcShape *>(document.shape(arcId));
    runner.check(arc->isValid(), QStringLiteral("正常三点弧应有效"));

    // 把弧上点拖到与两端点共线 → 弧应无效，且不产生 NaN
    document.movePoint(cId, Vec2(1.0, 0.0));
    runner.check(!arc->isValid(), QStringLiteral("三点共线时弧应变为无效"));
    runner.check(!arc->arcData().isDefined, QStringLiteral("无效弧的几何数据应标记为未定义"));

    // 恢复
    document.movePoint(cId, Vec2(2.0, 3.0));
    runner.check(arc->isValid(), QStringLiteral("共线状态解除后弧应恢复有效"));

    // 平行线：无交点
    const ShapeId dId = addFreePoint(&document, 0.0, 1.0);
    const ShapeId eId = addFreePoint(&document, 5.0, 1.0);
    const ShapeId parallelLineId = addLine(&document, LineKind::InfiniteLine, dId, eId);
    const ShapeId baseLineId = addLine(&document, LineKind::InfiniteLine, aId, bId);
    QList<Vec2> hits;
    const int count = document.curveIntersections(baseLineId, parallelLineId, &hits);
    runner.check(count == 0, QStringLiteral("平行直线不应有交点"),
                 QStringLiteral("实际 %1 个").arg(count));

    // 两点重合的线段应无效
    const ShapeId zeroLengthId = addLine(&document, LineKind::Segment, aId, aId);
    runner.check(!document.shape(zeroLengthId)->isValid(),
                 QStringLiteral("两端点重合的线段应无效，且文档不得崩溃"));

    // 相切：圆与直线恰有一个交点
    const ShapeId centerId = addFreePoint(&document, 0.0, 3.0);
    const ShapeId rimId = addFreePoint(&document, 3.0, 3.0);
    const ShapeId tangentCircleId = addCircle(&document, centerId, rimId);
    hits.clear();
    const int tangentCount = document.curveIntersections(baseLineId, tangentCircleId, &hits);
    runner.check(tangentCount == 1, QStringLiteral("与圆相切的直线应只有 1 个交点"),
                 QStringLiteral("实际 %1 个").arg(tangentCount));

    return runner.report();
}

// ---------------------------------------------------------------------------
// 用例集：拓扑序不变式与级联删除
// ---------------------------------------------------------------------------

/** @brief 验证"父 id 必然小于自身 id"与级联删除、相机变换。 */
int runStructureTests()
{
    TestRunner runner(QStringLiteral("结构与相机"));

    GeoDocument document;
    const ShapeId aId = addFreePoint(&document, 0.0, 0.0);
    const ShapeId bId = addFreePoint(&document, 6.0, 0.0);
    const ShapeId segmentId = addLine(&document, LineKind::Segment, aId, bId);
    const ShapeId midId = document.allocateShapeId();
    document.addShape(new MidPointShape(midId, aId, bId));

    // 子图重算范围
    const QSet<ShapeId> affected = document.descendantIds(QSet<ShapeId>() << aId);
    runner.check(affected.contains(segmentId) && affected.contains(midId),
                 QStringLiteral("移动端点应影响线段与中点"));
    runner.check(!affected.contains(bId), QStringLiteral("另一个端点不应被算作依赖者"));

    // 拓扑序不变式（通过 friend 测试外壳访问内部索引）
    {
        ShapeId violationId = kInvalidShapeId;
        ShapeId parentId = kInvalidShapeId;
        const bool orderOk =
            GeometrySmokeTest::verifyTopologicalOrder(document, &violationId, &parentId);
        runner.check(orderOk, QStringLiteral("必须满足「父 id < 自身 id」的拓扑序不变式"),
                     QStringLiteral("违反者：图元 %1 的父 %2").arg(violationId).arg(parentId));
    }

    // 级联删除：删掉点 a 应连带删除线段与中点
    const QList<ShapeId> removed = document.removeShapes(QSet<ShapeId>() << aId);
    runner.check(removed.size() == 3, QStringLiteral("删除端点应级联删除线段与中点（共 3 个）"),
                 QStringLiteral("实际删除 %1 个").arg(removed.size()));
    runner.check(document.shape(bId) != nullptr, QStringLiteral("未被依赖的另一端点应保留"));

    // 相机：屏幕 ↔ 场景往返一致
    {
        Camera camera;
        camera.setViewportSize(800, 600);
        camera.setZoom(40.0);
        camera.centerOn(Vec2(0.0, 0.0));
        const Vec2 scenePos(3.5, -2.25);
        const Vec2 roundTrip = camera.toScene(camera.toScreen(scenePos));
        runner.check(nearlyEqual(roundTrip, scenePos, 1e-9),
                     QStringLiteral("屏幕↔场景坐标往返应一致"));
    }

    // 相机：以鼠标为锚点缩放时锚点不动
    {
        Camera camera;
        camera.setViewportSize(800, 600);
        camera.setZoom(40.0);
        const Vec2 anchor(-2.0, 1.5);
        const QPointF screenBefore = camera.toScreen(anchor);
        camera.zoomAround(1.5, anchor);
        const QPointF screenAfter = camera.toScreen(anchor);
        runner.check(nearlyEqual(screenAfter.x(), screenBefore.x(), 1e-6)
                         && nearlyEqual(screenAfter.y(), screenBefore.y(), 1e-6),
                     QStringLiteral("以锚点缩放时锚点的屏幕位置应保持不变"),
                     QStringLiteral("缩放前 (%1,%2) 缩放后 (%3,%4)")
                         .arg(screenBefore.x()).arg(screenBefore.y())
                         .arg(screenAfter.x()).arg(screenAfter.y()));
    }

    // 网格步长必须是 1/2/5 × 10^n
    {
        Camera camera;
        camera.setViewportSize(800, 600);
        bool stepOk = true;
        for (int i = 0; i < 40; ++i) {
            camera.setZoom(0.05 * std::pow(1.2, i));
            const double step = camera.gridStep();
            const double exponent = std::floor(std::log10(step));
            const double normalized = step / std::pow(10.0, exponent);
            const bool isNice = nearlyEqual(normalized, 1.0, 1e-6)
                || nearlyEqual(normalized, 2.0, 1e-6) || nearlyEqual(normalized, 5.0, 1e-6);
            if (!isNice) {
                stepOk = false;
            }
        }
        runner.check(stepOk, QStringLiteral("网格步长在所有缩放级别下都应为 1/2/5 × 10^n"));
    }

    // 缩放上限于 300%（用户要求"最多放大到百分之三百"）
    {
        Camera camera;
        camera.setViewportSize(800, 600);
        camera.setZoom(100000.0);
        runner.check(camera.zoomPercent() == 300,
                     QStringLiteral("缩放上限应为 300%"),
                     QStringLiteral("实际 %1%").arg(camera.zoomPercent()));

        // 滚轮缩放也不能突破上限
        for (int i = 0; i < 30; ++i) {
            camera.zoomAround(1.25, Vec2(0.0, 0.0));
        }
        runner.check(camera.zoomPercent() == 300,
                     QStringLiteral("连续放大到顶后仍应停在 300%"),
                     QStringLiteral("实际 %1%").arg(camera.zoomPercent()));

        // 下限保持 5%（1/40 * ... 详见 Camera::minZoom）
        camera.setZoom(1e-9);
        runner.check(camera.zoomPercent() < 10,
                     QStringLiteral("缩放下限应仍能缩到很小的比例"),
                     QStringLiteral("实际 %1%").arg(camera.zoomPercent()));
    }

    // 坐标轴最小精度：默认每格 1，放大到 300% 也不允许更细
    {
        Camera camera;
        camera.setViewportSize(800, 600);
        runner.check(nearlyEqual(camera.minimumGridStep(), 1.0, 1e-12),
                     QStringLiteral("默认最小精度应为每格 1 个场景单位"));

        camera.setZoom(Camera::maxZoom());
        runner.check(camera.zoomPercent() == 300, QStringLiteral("先放大到 300% 再检查网格"));
        const double stepAtMaxZoom = camera.gridStep();
        runner.check(nearlyEqual(stepAtMaxZoom, 1.0, 1e-9),
                     QStringLiteral("放大到 300% 时网格步长仍应为 1（不低于最小精度）"),
                     QStringLiteral("实际 %1").arg(stepAtMaxZoom));

        // 无最小精度时网格本会细到 0.2：这里验证"没有这个下限就会出问题"，
        // 从而说明这个下限确实在起作用（否则上面的断言可能只是碰巧成立）
        camera.setMinimumGridStep(0.05);
        const double stepWithoutFloor = camera.gridStep();
        runner.check(stepWithoutFloor < 1.0,
                     QStringLiteral("把最小精度调小后网格应允许更细（证明下限确实在生效）"),
                     QStringLiteral("实际 %1").arg(stepWithoutFloor));

        // 复位并验证可选精度值都能生效
        camera.setMinimumGridStep(5.0);
        runner.check(nearlyEqual(camera.gridStep(), 5.0, 1e-9),
                     QStringLiteral("把最小精度设为 5 后网格步长应不小于 5"),
                     QStringLiteral("实际 %1").arg(camera.gridStep()));

        // 非法输入必须被忽略，而不是把状态污染成 NaN / 0
        const double before = camera.minimumGridStep();
        camera.setMinimumGridStep(0.0);
        camera.setMinimumGridStep(-3.0);
        camera.setMinimumGridStep(std::numeric_limits<double>::quiet_NaN());
        runner.check(nearlyEqual(camera.minimumGridStep(), before, 1e-12),
                     QStringLiteral("非法的最小精度（0 / 负数 / NaN）应被忽略"),
                     QStringLiteral("实际 %1").arg(camera.minimumGridStep()));
    }

    return runner.report();
}

// ---------------------------------------------------------------------------
// 性能基准
// ---------------------------------------------------------------------------

/**
 * @brief 构造一条深层依赖链，测量拖动时的子图重算耗时。
 * @param linkCount 链条节数（每节新增 1 个点、1 条线段、1 个中点）。
 * @param document 输出参数：构造好的文档。
 * @return 链首自由点的 id；拖动它会让**整条链**都落在受影响子图内。
 * @note  依赖类型必须匹配：线段的父必须是**点**，中点的父必须是**点**，
 *        因此链条沿"点 → 线段 → 中点 → 下一个点"推进，
 *        每个新点都挂在上一节的中点上，从而形成真正的传递依赖。
 */
ShapeId buildDependencyChain(int linkCount, GeoDocument *document)
{
    const ShapeId rootId = addFreePoint(document, 0.0, 0.0);
    ShapeId anchorId = rootId;

    for (int i = 0; i < linkCount; ++i) {
        const ShapeId nextPointId = addFreePoint(document, static_cast<double>(i + 1), 1.0);
        const ShapeId segmentId = addLine(document, LineKind::Segment, anchorId, nextPointId);
        const ShapeId midId = document->allocateShapeId();
        document->addShape(new MidPointShape(midId, anchorId, nextPointId));
        anchorId = midId; // 下一节的端点挂在本节中点上，形成传递依赖
        Q_UNUSED(segmentId);
    }
    return rootId;
}

/** @brief 测量拖动重算与命中测试的耗时，与需求中的 50ms 指标对照。 */
int runPerformanceBenchmark()
{
    TestRunner runner(QStringLiteral("性能基准"));

    // 规格：1000 个图元量级、拖动一个点，受影响子图重算应在 50ms 内完成。
    // 330 节链条 = 1 + 330×3 = 991 个图元，量级与需求一致。
    GeoDocument document;
    const ShapeId rootId = buildDependencyChain(330, &document);
    const int shapeCount = document.shapeCount();
    print(QStringLiteral("图元总数：%1").arg(shapeCount));

    const int iterations = 200;
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < iterations; ++i) {
        const double offset = (i % 2 == 0) ? 0.01 : -0.01;
        document.movePoint(rootId, Vec2(offset, offset));
    }
    const double totalMs = static_cast<double>(timer.elapsed());
    const double perMoveMs = totalMs / iterations;
    print(QStringLiteral("拖动重算：%1 次共 %2 ms，平均 %3 ms/次")
              .arg(iterations)
              .arg(totalMs, 0, 'f', 1)
              .arg(perMoveMs, 0, 'f', 4));

    runner.check(perMoveMs < 50.0,
                 QStringLiteral("单次拖动重算应快于 50ms（需求验收指标）"),
                 QStringLiteral("实际 %1 ms").arg(perMoveMs, 0, 'f', 4));

    // 全量命中测试：每次拖动都会调用一次
    const Vec2 probe(3.0, 0.5);
    timer.restart();
    const int hitIterations = 2000;
    for (int i = 0; i < hitIterations; ++i) {
        document.hitTestAny(probe, 0.2);
    }
    const double perHitMs = static_cast<double>(timer.elapsed()) / hitIterations;
    print(QStringLiteral("命中测试：%1 次平均 %2 ms/次").arg(hitIterations).arg(perHitMs, 0, 'f', 4));
    runner.check(perHitMs < 5.0,
                 QStringLiteral("单次命中测试应快于 5ms"),
                 QStringLiteral("实际 %1 ms").arg(perHitMs, 0, 'f', 4));

    return runner.report();
}

// ---------------------------------------------------------------------------
// 入口
// ---------------------------------------------------------------------------

/**
 * @brief 测试程序入口。
 * @param argc 参数个数。
 * @param argv 参数数组。
 * @return 全部用例通过返回 0，否则返回 1。
 * @note  只创建 QCoreApplication：本程序必须能在**没有任何 GUI** 的环境里运行，
 *        这本身就是 model 层未混入界面依赖的证据。
 */
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    print(QStringLiteral("==== GeoBoard 几何引擎自测 ===="));

    int failures = 0;
    failures += runMathTests();
    failures += runDependencyTests();
    failures += runAngleTests();
    failures += runDerivedShapeTests();
    failures += runDegenerateTests();
    failures += runStructureTests();
    failures += runPerformanceBenchmark();

    print(QStringLiteral("================================"));
    if (failures == 0) {
        print(QStringLiteral("结论：全部用例通过"));
        return 0;
    }
    print(QStringLiteral("结论：有 %1 个用例集存在失败项").arg(failures));
    return 1;
}
