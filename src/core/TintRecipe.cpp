#include "core/TintRecipe.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mcstudio {
namespace {

constexpr double kEpsilon = 1e-6;

QVector<TintCurvePoint> normalizedCurve(QVector<TintCurvePoint> points)
{
    if (points.size() < 2)
        return {{0.0, 0.0}, {1.0, 1.0}};

    for (TintCurvePoint &point : points) {
        point.x = qBound(0.0, point.x, 1.0);
        point.y = qBound(0.0, point.y, 1.0);
    }
    std::sort(points.begin(), points.end(),
              [](const TintCurvePoint &a, const TintCurvePoint &b) {
                  return a.x < b.x;
              });

    QVector<TintCurvePoint> unique;
    unique.reserve(points.size());
    for (const TintCurvePoint &point : points) {
        if (!unique.isEmpty() && std::abs(unique.last().x - point.x) < kEpsilon) {
            unique.last() = point;
        } else {
            unique.append(point);
        }
    }

    if (unique.first().x > kEpsilon)
        unique.prepend({0.0, 0.0});
    if (unique.last().x < 1.0 - kEpsilon)
        unique.append({1.0, 1.0});
    if (unique.size() < 2)
        return {{0.0, 0.0}, {1.0, 1.0}};
    return unique;
}

double hermite(double y0, double y1, double m0, double m1, double h, double t)
{
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double h00 = 2 * t3 - 3 * t2 + 1;
    const double h10 = t3 - 2 * t2 + t;
    const double h01 = -2 * t3 + 3 * t2;
    const double h11 = t3 - t2;
    return h00 * y0 + h10 * h * m0 + h01 * y1 + h11 * h * m1;
}

} // namespace

TintRecipe::TintRecipe()
{
    m_stops = {
        {0.0, QColor(QStringLiteral("#1a1a1a"))},
        {0.35, QColor(QStringLiteral("#7a7a7a"))},
        {0.75, QColor(QStringLiteral("#d6d6d6"))},
        {1.0, QColor(QStringLiteral("#ffffff"))},
    };
    m_curve = {{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}};
    m_strength = 100;
}

void TintRecipe::setStops(const QVector<TintStop> &stops)
{
    m_stops = stops;
    for (TintStop &stop : m_stops)
        stop.position = qBound(0.0, stop.position, 1.0);
    std::sort(m_stops.begin(), m_stops.end(),
              [](const TintStop &a, const TintStop &b) {
                  return a.position < b.position;
              });
    if (m_stops.isEmpty()) {
        m_stops = {{0.0, QColor(Qt::black)}, {1.0, QColor(Qt::white)}};
    } else if (m_stops.size() == 1) {
        m_stops.append({1.0, m_stops.first().color});
    }
}

void TintRecipe::setCurve(const QVector<TintCurvePoint> &curve)
{
    m_curve = normalizedCurve(curve);
}

void TintRecipe::setStrength(int strength)
{
    m_strength = qBound(0, strength, 100);
}

QColor TintRecipe::sample(double position) const
{
    if (m_stops.isEmpty())
        return Qt::white;

    const double value = qBound(0.0, position, 1.0);
    if (value <= m_stops.first().position)
        return m_stops.first().color;
    if (value >= m_stops.last().position)
        return m_stops.last().color;

    for (int index = 0; index + 1 < m_stops.size(); ++index) {
        const TintStop &a = m_stops.at(index);
        const TintStop &b = m_stops.at(index + 1);
        if (value < a.position || value > b.position)
            continue;
        const double span = b.position - a.position;
        const double t = span < kEpsilon ? 0.0 : (value - a.position) / span;
        const auto mix = [t](int from, int to) {
            return qRound(from + (to - from) * t);
        };
        return QColor(mix(a.color.red(), b.color.red()),
                      mix(a.color.green(), b.color.green()),
                      mix(a.color.blue(), b.color.blue()),
                      mix(a.color.alpha(), b.color.alpha()));
    }
    return m_stops.last().color;
}

double TintRecipe::mapLuminance(double value) const
{
    const QVector<TintCurvePoint> &points = m_curve;
    const double x = qBound(0.0, value, 1.0);
    if (points.size() < 2)
        return x;
    if (x <= points.first().x)
        return points.first().y;
    if (x >= points.last().x)
        return points.last().y;

    const int count = points.size();
    QVector<double> h(count - 1);
    QVector<double> delta(count - 1);
    for (int index = 0; index + 1 < count; ++index) {
        h[index] = qMax(kEpsilon, points.at(index + 1).x - points.at(index).x);
        delta[index] = (points.at(index + 1).y - points.at(index).y) / h[index];
    }

    QVector<double> slope(count, 0.0);
    slope[0] = delta.first();
    slope[count - 1] = delta.last();
    for (int index = 1; index + 1 < count; ++index) {
        if (delta.at(index - 1) * delta.at(index) <= 0.0)
            slope[index] = 0.0;
        else
            slope[index] = (delta.at(index - 1) + delta.at(index)) / 2.0;
    }

    for (int index = 0; index + 1 < count; ++index) {
        if (std::abs(delta.at(index)) < kEpsilon) {
            slope[index] = 0.0;
            slope[index + 1] = 0.0;
            continue;
        }
        const double alpha = slope.at(index) / delta.at(index);
        const double beta = slope.at(index + 1) / delta.at(index);
        const double magnitude = alpha * alpha + beta * beta;
        if (magnitude > 9.0) {
            const double scale = 3.0 / std::sqrt(magnitude);
            slope[index] = scale * alpha * delta.at(index);
            slope[index + 1] = scale * beta * delta.at(index);
        }
    }

    int segment = 0;
    for (int index = 0; index + 1 < count; ++index) {
        if (x >= points.at(index).x && x <= points.at(index + 1).x) {
            segment = index;
            break;
        }
    }
    const double t = qBound(0.0, (x - points.at(segment).x) / h.at(segment), 1.0);
    return qBound(0.0, hermite(points.at(segment).y, points.at(segment + 1).y,
                               slope.at(segment), slope.at(segment + 1),
                               h.at(segment), t),
                  1.0);
}

namespace {

QRect maskBounds(const QImage &mask)
{
    if (mask.isNull())
        return {};
    int left = mask.width();
    int top = mask.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < mask.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(mask.constScanLine(y));
        for (int x = 0; x < mask.width(); ++x) {
            if (qRed(row[x]) < 128)
                continue;
            left = qMin(left, x);
            right = qMax(right, x);
            top = qMin(top, y);
            bottom = qMax(bottom, y);
        }
    }
    if (right < left || bottom < top)
        return {};
    return QRect(QPoint(left, top), QPoint(right, bottom));
}

} // namespace

QImage TintRecipe::apply(const QImage &source, const QImage &mask) const
{
    if (source.isNull())
        return {};

    QImage image = source;
    if (image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);

    const bool useMask = !mask.isNull() && mask.size() == image.size();
    const QImage maskImage =
        useMask && mask.format() != QImage::Format_ARGB32
            ? mask.convertToFormat(QImage::Format_ARGB32)
            : mask;
    const QRect area = useMask ? maskBounds(maskImage) : image.rect();
    if (area.isEmpty())
        return image;

    QVector<QRgb> lut(256);
    for (int level = 0; level < 256; ++level) {
        const QColor color = sample(mapLuminance(level / 255.0));
        lut[level] = color.rgba();
    }

    const int strength = qBound(0, m_strength, 100);
    const double amount = strength / 100.0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        const QRgb *maskRow =
            useMask ? reinterpret_cast<const QRgb *>(maskImage.constScanLine(y))
                    : nullptr;
        for (int x = area.left(); x <= area.right(); ++x) {
            if (useMask && qRed(maskRow[x]) < 128)
                continue;
            const QRgb pixel = row[x];
            const int alpha = qAlpha(pixel);
            if (alpha == 0)
                continue;
            const int luminance = (54 * qRed(pixel) + 183 * qGreen(pixel)
                                   + 19 * qBlue(pixel))
                                  >> 8;
            const QRgb tinted = lut.at(qBound(0, luminance, 255));
            if (strength >= 100) {
                row[x] = qRgba(qRed(tinted), qGreen(tinted), qBlue(tinted), alpha);
            } else if (strength <= 0) {
                continue;
            } else {
                const auto blend = [amount](int from, int to) {
                    return qBound(0, qRound(from + (to - from) * amount), 255);
                };
                row[x] = qRgba(blend(qRed(pixel), qRed(tinted)),
                               blend(qGreen(pixel), qGreen(tinted)),
                               blend(qBlue(pixel), qBlue(tinted)), alpha);
            }
        }
    }
    return image;
}

QImage TintRecipe::applyGradient(const QImage &source, const TintRecipe &other,
                                 double angleDegrees, const QImage &mask) const
{
    if (source.isNull())
        return {};

    QImage image = source;
    if (image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);

    const bool useMask = !mask.isNull() && mask.size() == image.size();
    const QImage maskImage =
        useMask && mask.format() != QImage::Format_ARGB32
            ? mask.convertToFormat(QImage::Format_ARGB32)
            : mask;
    const QRect area = useMask ? maskBounds(maskImage) : image.rect();
    if (area.isEmpty())
        return image;

    QVector<QRgb> first(256);
    QVector<QRgb> second(256);
    for (int level = 0; level < 256; ++level) {
        const double mapped = mapLuminance(level / 255.0);
        first[level] = sample(mapped).rgba();
        second[level] = other.sample(mapped).rgba();
    }

    constexpr double kPi = 3.14159265358979323846;
    const double radians = angleDegrees * kPi / 180.0;
    const double dx = std::cos(radians);
    const double dy = std::sin(radians);

    const QPoint corners[4] = {area.topLeft(), area.topRight(), area.bottomLeft(),
                               area.bottomRight()};
    double minimum = std::numeric_limits<double>::max();
    double maximum = std::numeric_limits<double>::lowest();
    for (const QPoint &corner : corners) {
        const double projection = corner.x() * dx + corner.y() * dy;
        minimum = std::min(minimum, projection);
        maximum = std::max(maximum, projection);
    }
    const double span = qMax(1e-6, maximum - minimum);

    const int strength = qBound(0, m_strength, 100);
    const double amount = strength / 100.0;
    for (int y = area.top(); y <= area.bottom(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        const QRgb *maskRow =
            useMask ? reinterpret_cast<const QRgb *>(maskImage.constScanLine(y))
                    : nullptr;
        for (int x = area.left(); x <= area.right(); ++x) {
            if (useMask && qRed(maskRow[x]) < 128)
                continue;
            const QRgb pixel = row[x];
            const int alpha = qAlpha(pixel);
            if (alpha == 0)
                continue;
            const int luminance = (54 * qRed(pixel) + 183 * qGreen(pixel)
                                   + 19 * qBlue(pixel))
                                  >> 8;
            const int index = qBound(0, luminance, 255);
            const QRgb a = first.at(index);
            const QRgb b = second.at(index);
            const double t = qBound(0.0, ((x * dx + y * dy) - minimum) / span, 1.0);
            const int tintR = qRound(qRed(a) + (qRed(b) - qRed(a)) * t);
            const int tintG = qRound(qGreen(a) + (qGreen(b) - qGreen(a)) * t);
            const int tintB = qRound(qBlue(a) + (qBlue(b) - qBlue(a)) * t);

            if (strength >= 100) {
                row[x] = qRgba(tintR, tintG, tintB, alpha);
            } else if (strength <= 0) {
                continue;
            } else {
                const auto blend = [amount](int from, int to) {
                    return qBound(0, qRound(from + (to - from) * amount), 255);
                };
                row[x] = qRgba(blend(qRed(pixel), tintR),
                               blend(qGreen(pixel), tintG),
                               blend(qBlue(pixel), tintB), alpha);
            }
        }
    }
    return image;
}

QJsonObject TintRecipe::toJson() const
{
    QJsonObject object;
    object.insert(QStringLiteral("strength"), m_strength);

    QJsonArray stops;
    for (const TintStop &stop : m_stops) {
        QJsonObject entry;
        entry.insert(QStringLiteral("pos"), stop.position);
        entry.insert(QStringLiteral("color"), stop.color.name(QColor::HexArgb));
        stops.append(entry);
    }
    object.insert(QStringLiteral("stops"), stops);

    QJsonArray curve;
    for (const TintCurvePoint &point : m_curve) {
        QJsonObject entry;
        entry.insert(QStringLiteral("x"), point.x);
        entry.insert(QStringLiteral("y"), point.y);
        curve.append(entry);
    }
    object.insert(QStringLiteral("curve"), curve);
    return object;
}

TintRecipe TintRecipe::fromJson(const QJsonObject &object)
{
    TintRecipe recipe;
    if (object.isEmpty())
        return recipe;

    QVector<TintStop> stops;
    for (const QJsonValue &value : object.value(QStringLiteral("stops")).toArray()) {
        const QJsonObject entry = value.toObject();
        TintStop stop;
        stop.position = entry.value(QStringLiteral("pos")).toDouble();
        stop.color = QColor::fromString(entry.value(QStringLiteral("color")).toString());
        if (stop.color.isValid())
            stops.append(stop);
    }
    if (!stops.isEmpty())
        recipe.setStops(stops);

    QVector<TintCurvePoint> curve;
    for (const QJsonValue &value : object.value(QStringLiteral("curve")).toArray()) {
        const QJsonObject entry = value.toObject();
        curve.append({entry.value(QStringLiteral("x")).toDouble(),
                      entry.value(QStringLiteral("y")).toDouble()});
    }
    if (curve.size() >= 2)
        recipe.setCurve(curve);

    recipe.setStrength(object.value(QStringLiteral("strength")).toInt(100));
    return recipe;
}

QStringList TintRecipe::presetIds()
{
    return {QStringLiteral("iron"),     QStringLiteral("gold"),
            QStringLiteral("diamond"),  QStringLiteral("copper"),
            QStringLiteral("netherite"), QStringLiteral("emerald"),
            QStringLiteral("redstone"), QStringLiteral("lapis")};
}

QString TintRecipe::presetName(const QString &id)
{
    if (id == QLatin1String("iron"))
        return QStringLiteral("铁");
    if (id == QLatin1String("gold"))
        return QStringLiteral("金");
    if (id == QLatin1String("diamond"))
        return QStringLiteral("钻石");
    if (id == QLatin1String("copper"))
        return QStringLiteral("铜");
    if (id == QLatin1String("netherite"))
        return QStringLiteral("下界合金");
    if (id == QLatin1String("emerald"))
        return QStringLiteral("绿宝石");
    if (id == QLatin1String("redstone"))
        return QStringLiteral("红石");
    if (id == QLatin1String("lapis"))
        return QStringLiteral("青金石");
    return id;
}

TintRecipe TintRecipe::preset(const QString &id)
{
    TintRecipe recipe;
    QVector<TintStop> stops;
    const auto add = [&stops](double position, const char *color) {
        stops.append({position, QColor(QString::fromLatin1(color))});
    };

    if (id == QLatin1String("gold")) {
        add(0.0, "#2b1a05");
        add(0.35, "#8a5a12");
        add(0.7, "#e0b23a");
        add(0.9, "#ffe37a");
        add(1.0, "#fffbe0");
    } else if (id == QLatin1String("diamond")) {
        add(0.0, "#073232");
        add(0.35, "#157a7a");
        add(0.7, "#5fd6d6");
        add(0.9, "#b9ffff");
        add(1.0, "#ffffff");
    } else if (id == QLatin1String("copper")) {
        add(0.0, "#2a120a");
        add(0.35, "#7a3a1c");
        add(0.7, "#c46a38");
        add(0.9, "#e8a06a");
        add(1.0, "#ffd8b0");
    } else if (id == QLatin1String("netherite")) {
        add(0.0, "#120e12");
        add(0.35, "#3a3038");
        add(0.7, "#6a5a64");
        add(0.9, "#9a8a94");
        add(1.0, "#c8bcc4");
    } else if (id == QLatin1String("emerald")) {
        add(0.0, "#04240f");
        add(0.35, "#0f6a2e");
        add(0.7, "#2fc45c");
        add(0.9, "#8affb0");
        add(1.0, "#e8fff0");
    } else if (id == QLatin1String("redstone")) {
        add(0.0, "#2a0404");
        add(0.35, "#7a0c0c");
        add(0.7, "#c41a1a");
        add(0.9, "#ff5a5a");
        add(1.0, "#ffc0c0");
    } else if (id == QLatin1String("lapis")) {
        add(0.0, "#050a2a");
        add(0.35, "#14246a");
        add(0.7, "#2f52c4");
        add(0.9, "#6f96ff");
        add(1.0, "#c8d8ff");
    } else {
        add(0.0, "#1a1a1a");
        add(0.35, "#6a6a6a");
        add(0.7, "#b8b8b8");
        add(0.9, "#e8e8e8");
        add(1.0, "#ffffff");
    }

    recipe.setStops(stops);
    recipe.setCurve({{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}});
    recipe.setStrength(100);
    return recipe;
}

QColor TintRecipe::grayscale(double level)
{
    const int value = qBound(0, qRound(level * 255.0), 255);
    return QColor(value, value, value);
}

} // namespace mcstudio