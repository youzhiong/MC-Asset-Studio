#pragma once

#include <QImage>
#include <QRgb>
#include <QVector>

namespace mcstudio {

// Reduces the number of distinct colours while keeping the alpha channel. The
// palette is built with median cut and colours are mapped with an approximate
// perceptual (OKLab) distance.
struct ColorReduceResult {
    QImage image;
    int beforeColors = 0;
    int afterColors = 0;
};

int countUniqueColors(const QVector<QImage> &images);
QVector<QRgb> buildReducedPalette(const QVector<QImage> &images, int targetColors);
QImage reduceColors(const QImage &image, const QVector<QRgb> &palette);
ColorReduceResult reduceImage(const QImage &image, int targetColors);

} // namespace mcstudio
