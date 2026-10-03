#include "app/Easing.h"

#include <QPointF>

namespace mcsm {
namespace Easing {
namespace {

double g_speedFactor = 1.0;

QEasingCurve fromPoints(qreal x1, qreal y1, qreal x2, qreal y2)
{
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(x1, y1), QPointF(x2, y2), QPointF(1.0, 1.0));
    return curve;
}

} // namespace

QEasingCurve standard()
{
    return fromPoints(0.22, 1.0, 0.36, 1.0);
}

QEasingCurve emphasized()
{
    return fromPoints(0.45, 0.05, 0.15, 1.0);
}

QEasingCurve overshoot()
{
    return fromPoints(0.34, 1.56, 0.64, 1.0);
}

QEasingCurve entrance()
{
    return fromPoints(0.16, 1.0, 0.3, 1.0);
}

int fast() { return scaled(140); }
int normal() { return scaled(260); }
int slow() { return scaled(420); }

void setSpeedFactor(double factor)
{
    g_speedFactor = qBound(0.25, factor, 3.0);
}

double speedFactor()
{
    return g_speedFactor;
}

int scaled(int milliseconds)
{
    return qMax(40, int(double(milliseconds) * g_speedFactor));
}

} // namespace Easing
} // namespace mcsm
