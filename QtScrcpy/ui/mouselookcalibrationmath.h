#pragma once
#include <QJsonObject>
#include <QPointF>
#include <QtMath>

namespace MouseLookCalibrationMath {
inline QPointF ratios(const QJsonObject &mouse)
{
    const double legacy = mouse.value("speedRatio").toDouble(1);
    return {mouse.value("speedRatioX").toDouble(legacy),
        mouse.value("speedRatioY").toDouble(mouse.contains("speedRatio") ? legacy / 2.25 : 1)};
}

// Both distances use logical pixels in the video widget. The input converter
// divides by speedRatio, so overshoot requires a LARGER ratio.
inline double correctedRatio(double ratio, double mouseDistance, double sceneDistance)
{
    if (!qIsFinite(ratio) || !qIsFinite(mouseDistance) || !qIsFinite(sceneDistance)
        || ratio < .001 || qAbs(mouseDistance) < 1 || qAbs(sceneDistance) < 1) { return 0; }
    const double result = ratio * qAbs(sceneDistance / mouseDistance);
    return qIsFinite(result) && result >= .001 && result <= 1000000 ? result : 0;
}

inline bool consistent(double first, double second)
{
    // Signed gains also detect selecting the wrong side of the landmark.
    return first * second > 0 && qAbs(first - second) <= .25 * qMax(qAbs(first), qAbs(second));
}

inline QJsonObject withRatios(QJsonObject mouse, QPointF value)
{
    mouse.remove("speedRatio");
    mouse["speedRatioX"] = value.x();
    mouse["speedRatioY"] = value.y();
    return mouse;
}
}
