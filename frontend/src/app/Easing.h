#pragma once

#include <QEasingCurve>

namespace mcsm {

/// Shared motion language: all transitions use bezier splines so that movement
/// and fades feel continuous instead of linear.
namespace Easing {

/// Standard material "ease out" curve, used for entrances and page slides.
QEasingCurve standard();
/// Stronger ease-in-out used for larger movements (page switch, sidebar).
QEasingCurve emphasized();
/// Slight overshoot, used for toasts and toggle knobs.
QEasingCurve overshoot();
/// Gentle deceleration for cards and list items appearing.
QEasingCurve entrance();

/// Base durations (ms), scaled by the user animation speed setting.
int fast();
int normal();
int slow();

/// 0.5 = fast, 1.0 = default, 1.5 = relaxed
void setSpeedFactor(double factor);
double speedFactor();
int scaled(int milliseconds);

} // namespace Easing

} // namespace mcsm
