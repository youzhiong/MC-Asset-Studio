#pragma once

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mcstudio {

struct TintStop {
    double position = 0.0; // 0 = darkest, 1 = brightest
    QColor color;
};

struct TintCurvePoint {
    double x = 0.0;
    double y = 0.0;
};

// Grayscale-to-colour recipe: optional curve remaps the source luminance, then a
// gradient ramp maps it to colour. Applying is destructive but one editor undo.
class TintRecipe
{
public:
    TintRecipe();

    QVector<TintStop> stops() const { return m_stops; }
    void setStops(const QVector<TintStop> &stops);
    QVector<TintCurvePoint> curve() const { return m_curve; }
    void setCurve(const QVector<TintCurvePoint> &curve);

    int strength() const { return m_strength; }
    void setStrength(int strength);

    QColor sample(double position) const;
    double mapLuminance(double value) const;
    // A null/empty mask means the whole image. Otherwise only pixels whose red
    // channel is non-zero are changed.
    QImage apply(const QImage &source, const QImage &mask = {}) const;
    // Two recipes blended along a linear direction (0° = left→right,
    // 90° = top→bottom).
    QImage applyGradient(const QImage &source, const TintRecipe &other,
                         double angleDegrees, const QImage &mask = {}) const;

    QJsonObject toJson() const;
    static TintRecipe fromJson(const QJsonObject &object);

    static QStringList presetIds();
    static QString presetName(const QString &id);
    static TintRecipe preset(const QString &id);

    static QColor grayscale(double level);

private:
    QVector<TintStop> m_stops;
    QVector<TintCurvePoint> m_curve;
    int m_strength = 100;
};

} // namespace mcstudio
