#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>

namespace mcstudio {
namespace PixelEdit {

enum class Symmetry {
    None = 0,
    Horizontal = 1, // mirror left/right
    Vertical = 2,   // mirror top/bottom
    Both = 3,
};

QColor colorAt(const QImage &image, int x, int y);

// All drawing helpers write the given color verbatim (no alpha blending), so
// transparent pencil/eraser strokes really clear pixels instead of darkening
// them. Coordinates outside the image are ignored.
void drawPoint(QImage &image, const QPoint &point, const QColor &color, int brushSize);
void drawLine(QImage &image, const QPoint &from, const QPoint &to, const QColor &color,
              int brushSize);
void drawRect(QImage &image, const QRect &rect, const QColor &color, int brushSize,
              bool fill);
void drawEllipse(QImage &image, const QRect &rect, const QColor &color, int brushSize,
                 bool fill);

void floodFill(QImage &image, const QPoint &start, const QColor &color, bool contiguous,
               int tolerance);
int replaceColor(QImage &image, const QColor &target, const QColor &color, int tolerance);

QVector<QPoint> mirrorPoints(const QPoint &point, const QSize &size, Symmetry symmetry);
QRect mirrorRect(const QRect &rect, const QSize &size, Symmetry symmetry);
QImage cropFrame(const QImage &image, int frameIndex, const QSize &frameSize);

} // namespace PixelEdit
} // namespace mcstudio
