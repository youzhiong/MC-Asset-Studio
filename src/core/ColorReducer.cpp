#include "core/ColorReducer.h"

#include <QHash>
#include <QSet>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mcstudio {
namespace {

struct Lab {
    double l = 0.0;
    double a = 0.0;
    double b = 0.0;
};

double srgbToLinear(int value)
{
    const double channel = value / 255.0;
    return channel <= 0.04045 ? channel / 12.92
                              : std::pow((channel + 0.055) / 1.055, 2.4);
}

Lab toLab(int r, int g, int b)
{
    const double lr = srgbToLinear(r);
    const double lg = srgbToLinear(g);
    const double lb = srgbToLinear(b);

    const double l = 0.4122214708 * lr + 0.5363325363 * lg + 0.0514459929 * lb;
    const double m = 0.2119034982 * lr + 0.6806995451 * lg + 0.1073969566 * lb;
    const double s = 0.0883024619 * lr + 0.2817188376 * lg + 0.6299787005 * lb;

    const double lc = std::cbrt(l);
    const double mc = std::cbrt(m);
    const double sc = std::cbrt(s);

    Lab lab;
    lab.l = 0.2104542553 * lc + 0.7936177850 * mc - 0.0040720468 * sc;
    lab.a = 1.9779984951 * lc - 2.4285922050 * mc + 0.4505937099 * sc;
    lab.b = 0.0259040371 * lc + 0.7827717662 * mc - 0.8086757660 * sc;
    return lab;
}

double distanceSquared(const Lab &a, const Lab &b)
{
    const double dl = a.l - b.l;
    const double da = a.a - b.a;
    const double db = a.b - b.b;
    return dl * dl + da * da + db * db;
}

struct ColorEntry {
    int r = 0;
    int g = 0;
    int b = 0;
    int count = 0;
};

struct Box {
    QVector<int> indices;
    int minR = 0, maxR = 0;
    int minG = 0, maxG = 0;
    int minB = 0, maxB = 0;
    int pixels = 0;
};

void refreshBox(const QVector<ColorEntry> &colors, Box &box)
{
    if (box.indices.isEmpty())
        return;
    box.minR = box.minG = box.minB = 255;
    box.maxR = box.maxG = box.maxB = 0;
    box.pixels = 0;
    for (int index : box.indices) {
        const ColorEntry &color = colors.at(index);
        box.minR = qMin(box.minR, color.r);
        box.maxR = qMax(box.maxR, color.r);
        box.minG = qMin(box.minG, color.g);
        box.maxG = qMax(box.maxG, color.g);
        box.minB = qMin(box.minB, color.b);
        box.maxB = qMax(box.maxB, color.b);
        box.pixels += color.count;
    }
}

QHash<QRgb, int> histogram(const QVector<QImage> &images)
{
    QHash<QRgb, int> counts;
    for (const QImage &source : images) {
        if (source.isNull())
            continue;
        const QImage image = source.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < image.height(); ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x) {
                const QRgb pixel = row[x];
                if (qAlpha(pixel) == 0)
                    continue;
                ++counts[qRgb(qRed(pixel), qGreen(pixel), qBlue(pixel))];
            }
        }
    }
    return counts;
}

} // namespace

int countUniqueColors(const QVector<QImage> &images)
{
    return histogram(images).size();
}

QVector<QRgb> buildReducedPalette(const QVector<QImage> &images, int targetColors)
{
    const int target = qBound(2, targetColors, 256);
    const QHash<QRgb, int> counts = histogram(images);
    if (counts.isEmpty())
        return {};

    QVector<ColorEntry> colors;
    colors.reserve(counts.size());
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        ColorEntry entry;
        entry.count = it.value();
        entry.r = qRed(it.key());
        entry.g = qGreen(it.key());
        entry.b = qBlue(it.key());
        colors.append(entry);
    }

    if (colors.size() <= target) {
        QVector<QRgb> palette;
        palette.reserve(colors.size());
        for (const ColorEntry &entry : colors)
            palette.append(qRgb(entry.r, entry.g, entry.b));
        return palette;
    }

    Box initial;
    initial.indices.reserve(colors.size());
    for (int index = 0; index < colors.size(); ++index)
        initial.indices.append(index);
    refreshBox(colors, initial);

    QVector<Box> boxes;
    boxes.append(initial);

    while (boxes.size() < target) {
        int best = -1;
        double bestScore = 0.0;
        for (int index = 0; index < boxes.size(); ++index) {
            const Box &box = boxes.at(index);
            if (box.indices.size() < 2)
                continue;
            const int range = qMax(box.maxR - box.minR,
                                   qMax(box.maxG - box.minG, box.maxB - box.minB));
            const double score = double(range) * double(box.pixels);
            if (score > bestScore) {
                bestScore = score;
                best = index;
            }
        }
        if (best < 0)
            break;

        Box box = boxes.at(best);
        const int rangeR = box.maxR - box.minR;
        const int rangeG = box.maxG - box.minG;
        const int rangeB = box.maxB - box.minB;
        int channel = 0;
        if (rangeG >= rangeR && rangeG >= rangeB)
            channel = 1;
        else if (rangeB >= rangeR && rangeB >= rangeG)
            channel = 2;

        std::sort(box.indices.begin(), box.indices.end(),
                  [&colors, channel](int a, int b) {
                      const ColorEntry &ca = colors.at(a);
                      const ColorEntry &cb = colors.at(b);
                      if (channel == 0)
                          return ca.r < cb.r;
                      if (channel == 1)
                          return ca.g < cb.g;
                      return ca.b < cb.b;
                  });

        int total = 0;
        for (int index : box.indices)
            total += colors.at(index).count;

        int accumulated = 0;
        int split = 0;
        for (; split < box.indices.size() - 1; ++split) {
            accumulated += colors.at(box.indices.at(split)).count;
            if (accumulated * 2 >= total)
                break;
        }
        split = qBound(0, split, box.indices.size() - 2);

        Box left;
        Box right;
        left.indices = box.indices.mid(0, split + 1);
        right.indices = box.indices.mid(split + 1);
        refreshBox(colors, left);
        refreshBox(colors, right);

        boxes[best] = left;
        boxes.append(right);
    }

    QSet<QRgb> unique;
    QVector<QRgb> palette;
    for (const Box &box : boxes) {
        qint64 totalR = 0;
        qint64 totalG = 0;
        qint64 totalB = 0;
        qint64 total = 0;
        for (int index : box.indices) {
            const ColorEntry &entry = colors.at(index);
            totalR += qint64(entry.r) * entry.count;
            totalG += qint64(entry.g) * entry.count;
            totalB += qint64(entry.b) * entry.count;
            total += entry.count;
        }
        if (total <= 0)
            continue;
        const QRgb color = qRgb(qRound(totalR / double(total)),
                                qRound(totalG / double(total)),
                                qRound(totalB / double(total)));
        if (!unique.contains(color)) {
            unique.insert(color);
            palette.append(color);
        }
    }
    return palette;
}

QImage reduceColors(const QImage &source, const QVector<QRgb> &palette)
{
    if (source.isNull() || palette.isEmpty())
        return source;

    QImage image = source;
    if (image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);

    QVector<Lab> paletteLab;
    paletteLab.reserve(palette.size());
    for (QRgb color : palette)
        paletteLab.append(toLab(qRed(color), qGreen(color), qBlue(color)));

    QHash<QRgb, QRgb> cache;
    cache.reserve(256);
    for (int y = 0; y < image.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = row[x];
            const int alpha = qAlpha(pixel);
            if (alpha == 0)
                continue;
            const QRgb rgb = qRgb(qRed(pixel), qGreen(pixel), qBlue(pixel));
            auto cached = cache.constFind(rgb);
            QRgb replacement;
            if (cached != cache.constEnd()) {
                replacement = cached.value();
            } else {
                const Lab lab = toLab(qRed(rgb), qGreen(rgb), qBlue(rgb));
                double best = std::numeric_limits<double>::max();
                QRgb bestColor = palette.first();
                for (int index = 0; index < palette.size(); ++index) {
                    const double distance = distanceSquared(lab, paletteLab.at(index));
                    if (distance < best) {
                        best = distance;
                        bestColor = palette.at(index);
                    }
                }
                replacement = bestColor;
                cache.insert(rgb, replacement);
            }
            row[x] = qRgba(qRed(replacement), qGreen(replacement),
                           qBlue(replacement), alpha);
        }
    }
    return image;
}

ColorReduceResult reduceImage(const QImage &image, int targetColors)
{
    ColorReduceResult result;
    result.image = image;
    result.beforeColors = countUniqueColors({image});
    const QVector<QRgb> palette = buildReducedPalette({image}, targetColors);
    if (!palette.isEmpty()) {
        result.image = reduceColors(image, palette);
        result.afterColors = palette.size();
    }
    return result;
}

} // namespace mcstudio
