#include "core/PixelEdit.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace mcstudio {
namespace PixelEdit {
namespace {

void ensureArgb32(QImage &image)
{
    if (!image.isNull() && image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);
}

inline QRgb *rowAt(QImage &image, int y)
{
    return reinterpret_cast<QRgb *>(image.scanLine(y));
}

inline const QRgb *rowAt(const QImage &image, int y)
{
    return reinterpret_cast<const QRgb *>(image.scanLine(y));
}

bool sameColor(QRgb a, QRgb b, int tolerance)
{
    if (tolerance <= 0)
        return a == b;
    return qAbs(qRed(a) - qRed(b)) <= tolerance
           && qAbs(qGreen(a) - qGreen(b)) <= tolerance
           && qAbs(qBlue(a) - qBlue(b)) <= tolerance
           && qAbs(qAlpha(a) - qAlpha(b)) <= tolerance;
}

void putPixel(QImage &image, int x, int y, const QColor &color)
{
    if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
        return;
    rowAt(image, y)[x] = color.rgba();
}

} // namespace

QColor colorAt(const QImage &image, int x, int y)
{
    if (image.isNull() || x < 0 || y < 0 || x >= image.width() || y >= image.height())
        return Qt::transparent;
    return QColor::fromRgba(rowAt(image, y)[x]);
}

void drawPoint(QImage &image, const QPoint &point, const QColor &color, int brushSize)
{
    if (image.isNull())
        return;
    ensureArgb32(image);

    const int size = qBound(1, brushSize, 64);
    const int half = size / 2;
    for (int dy = -half; dy < size - half; ++dy) {
        for (int dx = -half; dx < size - half; ++dx)
            putPixel(image, point.x() + dx, point.y() + dy, color);
    }
}

void drawLine(QImage &image, const QPoint &from, const QPoint &to, const QColor &color,
              int brushSize)
{
    if (image.isNull())
        return;

    int x0 = from.x();
    int y0 = from.y();
    const int x1 = to.x();
    const int y1 = to.y();

    const int dx = qAbs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -qAbs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        drawPoint(image, QPoint(x0, y0), color, brushSize);
        if (x0 == x1 && y0 == y1)
            break;
        const int e2 = 2 * error;
        if (e2 >= dy) {
            error += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void drawRect(QImage &image, const QRect &rect, const QColor &color, int brushSize, bool fill)
{
    if (image.isNull() || rect.isEmpty())
        return;
    const QRect r = rect.normalized();

    if (fill) {
        for (int y = r.top(); y <= r.bottom(); ++y) {
            for (int x = r.left(); x <= r.right(); ++x)
                putPixel(image, x, y, color);
        }
        return;
    }

    drawLine(image, r.topLeft(), r.topRight(), color, brushSize);
    drawLine(image, r.topRight(), r.bottomRight(), color, brushSize);
    drawLine(image, r.bottomRight(), r.bottomLeft(), color, brushSize);
    drawLine(image, r.bottomLeft(), r.topLeft(), color, brushSize);
}

void drawEllipse(QImage &image, const QRect &rect, const QColor &color, int brushSize,
                 bool fill)
{
    if (image.isNull() || rect.isEmpty())
        return;
    const QRect r = rect.normalized();

    const double cx = r.x() + (r.width() - 1) / 2.0;
    const double cy = r.y() + (r.height() - 1) / 2.0;
    const double rx = qMax(0.5, r.width() / 2.0);
    const double ry = qMax(0.5, r.height() / 2.0);

    if (fill) {
        for (int y = r.top(); y <= r.bottom(); ++y) {
            const double ny = (y - cy) / ry;
            if (qAbs(ny) > 1.0)
                continue;
            const double span = rx * std::sqrt(qMax(0.0, 1.0 - ny * ny));
            const int left = qRound(cx - span);
            const int right = qRound(cx + span);
            for (int x = left; x <= right; ++x)
                putPixel(image, x, y, color);
        }
        return;
    }

    constexpr double kPi = 3.14159265358979323846;
    const int steps = qBound(32, 8 * (r.width() + r.height()), 4096);
    int previousX = qRound(cx + rx);
    int previousY = qRound(cy);
    for (int step = 1; step <= steps; ++step) {
        const double angle = (2.0 * kPi * step) / steps;
        const int x = qRound(cx + rx * std::cos(angle));
        const int y = qRound(cy + ry * std::sin(angle));
        drawLine(image, QPoint(previousX, previousY), QPoint(x, y), color, brushSize);
        previousX = x;
        previousY = y;
    }
}

void floodFill(QImage &image, const QPoint &start, const QColor &color, bool contiguous,
               int tolerance)
{
    if (image.isNull() || start.x() < 0 || start.y() < 0
        || start.x() >= image.width() || start.y() >= image.height()) {
        return;
    }
    ensureArgb32(image);

    const int toleranceValue = qBound(0, tolerance, 255);
    const QRgb target = rowAt(image, start.y())[start.x()];
    const QRgb replacement = color.rgba();
    if (target == replacement && toleranceValue == 0)
        return;

    const int width = image.width();
    const int height = image.height();

    if (!contiguous) {
        for (int y = 0; y < height; ++y) {
            QRgb *row = rowAt(image, y);
            for (int x = 0; x < width; ++x) {
                if (sameColor(row[x], target, toleranceValue))
                    row[x] = replacement;
            }
        }
        return;
    }

    QVector<quint8> visited(width * height, 0);
    QVector<QPoint> stack;
    stack.reserve(qMin(width * height, 4096));
    stack.append(start);

    while (!stack.isEmpty()) {
        const QPoint point = stack.takeLast();
        const int index = point.y() * width + point.x();
        if (visited[index])
            continue;
        visited[index] = 1;

        QRgb *row = rowAt(image, point.y());
        if (!sameColor(row[point.x()], target, toleranceValue))
            continue;
        row[point.x()] = replacement;

        if (point.x() > 0)
            stack.append(QPoint(point.x() - 1, point.y()));
        if (point.x() + 1 < width)
            stack.append(QPoint(point.x() + 1, point.y()));
        if (point.y() > 0)
            stack.append(QPoint(point.x(), point.y() - 1));
        if (point.y() + 1 < height)
            stack.append(QPoint(point.x(), point.y() + 1));
    }
}

void replaceColor(QImage &image, const QColor &target, const QColor &color, int tolerance)
{
    if (image.isNull())
        return;
    ensureArgb32(image);

    const int toleranceValue = qBound(0, tolerance, 255);
    const QRgb targetRgba = target.rgba();
    const QRgb replacement = color.rgba();
    if (targetRgba == replacement && toleranceValue == 0)
        return;

    for (int y = 0; y < image.height(); ++y) {
        QRgb *row = rowAt(image, y);
        for (int x = 0; x < image.width(); ++x) {
            if (sameColor(row[x], targetRgba, toleranceValue))
                row[x] = replacement;
        }
    }
}

QVector<QPoint> mirrorPoints(const QPoint &point, const QSize &size, Symmetry symmetry)
{
    QVector<QPoint> points;
    const int mode = static_cast<int>(symmetry);
    if (mode <= 0 || size.width() <= 0 || size.height() <= 0) {
        points.append(point);
        return points;
    }

    const bool mirrorX = (mode & static_cast<int>(Symmetry::Horizontal)) != 0;
    const bool mirrorY = (mode & static_cast<int>(Symmetry::Vertical)) != 0;

    const int mirroredX = size.width() - 1 - point.x();
    const int mirroredY = size.height() - 1 - point.y();

    points.append(point);
    if (mirrorX && mirroredX != point.x())
        points.append(QPoint(mirroredX, point.y()));
    if (mirrorY && mirroredY != point.y())
        points.append(QPoint(point.x(), mirroredY));
    if (mirrorX && mirrorY && (mirroredX != point.x() || mirroredY != point.y()))
        points.append(QPoint(mirroredX, mirroredY));
    return points;
}

QRect mirrorRect(const QRect &rect, const QSize &size, Symmetry symmetry)
{
    QRect result = rect.normalized();
    const int mode = static_cast<int>(symmetry);
    if ((mode & static_cast<int>(Symmetry::Horizontal)) != 0)
        result.moveLeft(size.width() - result.x() - result.width());
    if ((mode & static_cast<int>(Symmetry::Vertical)) != 0)
        result.moveTop(size.height() - result.y() - result.height());
    return result;
}

QImage cropFrame(const QImage &image, int frameIndex, const QSize &frameSize)
{
    if (image.isNull() || frameSize.width() <= 0 || frameSize.height() <= 0
        || frameIndex < 0) {
        return {};
    }
    if (image.size() == frameSize)
        return image;
    const int y = frameIndex * frameSize.height();
    if (y + frameSize.height() > image.height())
        return {};
    return image.copy(0, y, frameSize.width(), frameSize.height());
}

} // namespace PixelEdit
} // namespace mcstudio
