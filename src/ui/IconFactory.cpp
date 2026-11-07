#include "ui/IconFactory.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>

namespace {

void preparePainter(QPainter &painter, const QColor &color, qreal width = 1.55)
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
}

} // namespace

namespace IconFactory {

QPixmap glyphPixmap(Glyph glyph, const QColor &color, int size)
{
    const int logicalSize = qMax(12, size);
    QPixmap pixmap(logicalSize * 2, logicalSize * 2);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    preparePainter(painter, color);

    const qreal w = logicalSize;
    const qreal h = logicalSize;

    const auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) {
        painter.drawLine(QPointF(x1 * w, y1 * h), QPointF(x2 * w, y2 * h));
    };

    switch (glyph) {
    case Glyph::Plus:
        line(0.50, 0.22, 0.50, 0.78);
        line(0.22, 0.50, 0.78, 0.50);
        break;

    case Glyph::Folder: {
        QPainterPath path;
        path.moveTo(0.15 * w, 0.30 * h);
        path.lineTo(0.40 * w, 0.30 * h);
        path.lineTo(0.48 * w, 0.40 * h);
        path.lineTo(0.85 * w, 0.40 * h);
        path.lineTo(0.85 * w, 0.79 * h);
        path.lineTo(0.15 * w, 0.79 * h);
        path.closeSubpath();
        painter.drawPath(path);
        break;
    }

    case Glyph::Import:
        line(0.20, 0.56, 0.20, 0.79);
        line(0.20, 0.79, 0.80, 0.79);
        line(0.80, 0.79, 0.80, 0.56);
        line(0.50, 0.20, 0.50, 0.58);
        line(0.34, 0.44, 0.50, 0.60);
        line(0.66, 0.44, 0.50, 0.60);
        break;

    case Glyph::Save:
        painter.drawRoundedRect(QRectF(0.18 * w, 0.14 * h, 0.64 * w, 0.72 * h), 1.8, 1.8);
        painter.drawRect(QRectF(0.32 * w, 0.18 * h, 0.36 * w, 0.18 * h));
        line(0.30, 0.72, 0.70, 0.72);
        break;

    case Glyph::Export:
        line(0.20, 0.56, 0.20, 0.79);
        line(0.20, 0.79, 0.80, 0.79);
        line(0.80, 0.79, 0.80, 0.56);
        line(0.50, 0.60, 0.50, 0.20);
        line(0.34, 0.36, 0.50, 0.20);
        line(0.66, 0.36, 0.50, 0.20);
        break;

    case Glyph::Search:
        painter.drawEllipse(QRectF(0.18 * w, 0.17 * h, 0.47 * w, 0.47 * h));
        line(0.64, 0.64, 0.84, 0.84);
        break;

    case Glyph::Grid:
        painter.drawRoundedRect(QRectF(0.16 * w, 0.16 * h, 0.30 * w, 0.30 * h), 1.5, 1.5);
        painter.drawRoundedRect(QRectF(0.54 * w, 0.16 * h, 0.30 * w, 0.30 * h), 1.5, 1.5);
        painter.drawRoundedRect(QRectF(0.16 * w, 0.54 * h, 0.30 * w, 0.30 * h), 1.5, 1.5);
        painter.drawRoundedRect(QRectF(0.54 * w, 0.54 * h, 0.30 * w, 0.30 * h), 1.5, 1.5);
        break;

    case Glyph::List:
        painter.setBrush(color);
        painter.drawEllipse(QPointF(0.20 * w, 0.26 * h), 0.035 * w, 0.035 * h);
        painter.drawEllipse(QPointF(0.20 * w, 0.50 * h), 0.035 * w, 0.035 * h);
        painter.drawEllipse(QPointF(0.20 * w, 0.74 * h), 0.035 * w, 0.035 * h);
        painter.setBrush(Qt::NoBrush);
        line(0.32, 0.26, 0.82, 0.26);
        line(0.32, 0.50, 0.82, 0.50);
        line(0.32, 0.74, 0.82, 0.74);
        break;

    case Glyph::Cube: {
        const QPolygonF outline({
            QPointF(0.50 * w, 0.12 * h),
            QPointF(0.86 * w, 0.32 * h),
            QPointF(0.86 * w, 0.68 * h),
            QPointF(0.50 * w, 0.88 * h),
            QPointF(0.14 * w, 0.68 * h),
            QPointF(0.14 * w, 0.32 * h),
        });
        painter.drawPolygon(outline);
        line(0.50, 0.50, 0.50, 0.88);
        line(0.50, 0.50, 0.14, 0.68);
        line(0.50, 0.50, 0.86, 0.68);
        line(0.50, 0.50, 0.50, 0.12);
        break;
    }

    case Glyph::Image:
        painter.drawRoundedRect(QRectF(0.14 * w, 0.20 * h, 0.72 * w, 0.60 * h), 1.8, 1.8);
        painter.drawEllipse(QPointF(0.34 * w, 0.38 * h), 0.055 * w, 0.055 * h);
        line(0.20, 0.72, 0.40, 0.52);
        line(0.40, 0.52, 0.55, 0.66);
        line(0.55, 0.66, 0.66, 0.55);
        line(0.66, 0.55, 0.80, 0.70);
        break;

    case Glyph::Layers: {
        QPolygonF layer;
        layer << QPointF(0.50 * w, 0.16 * h) << QPointF(0.84 * w, 0.32 * h)
              << QPointF(0.50 * w, 0.48 * h) << QPointF(0.16 * w, 0.32 * h);
        painter.drawPolygon(layer);
        line(0.16, 0.46, 0.50, 0.62);
        line(0.50, 0.62, 0.84, 0.46);
        line(0.16, 0.60, 0.50, 0.76);
        line(0.50, 0.76, 0.84, 0.60);
        break;
    }

    case Glyph::Info:
        painter.drawEllipse(QRectF(0.18 * w, 0.18 * h, 0.64 * w, 0.64 * h));
        line(0.50, 0.44, 0.50, 0.68);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(0.50 * w, 0.32 * h), 0.035 * w, 0.035 * h);
        break;

    case Glyph::Play: {
        const QPolygonF triangle({
            QPointF(0.30 * w, 0.18 * h),
            QPointF(0.82 * w, 0.50 * h),
            QPointF(0.30 * w, 0.82 * h),
        });
        painter.setBrush(color);
        painter.drawPolygon(triangle);
        break;
    }

    case Glyph::ChevronDown:
        line(0.24, 0.38, 0.50, 0.64);
        line(0.50, 0.64, 0.76, 0.38);
        break;

    case Glyph::Close:
        line(0.26, 0.26, 0.74, 0.74);
        line(0.74, 0.26, 0.26, 0.74);
        break;
    }

    return pixmap;
}

QIcon icon(Glyph glyph, const QColor &color, int size)
{
    QIcon result;
    result.addPixmap(glyphPixmap(glyph, color, size), QIcon::Normal);
    result.addPixmap(glyphPixmap(glyph, QColor(75, 85, 99), size), QIcon::Disabled);
    return result;
}

QIcon appIcon()
{
    QIcon icon;
    // Prefer the multi-size ICO for the Win32 window/taskbar icon. Fall back to
    // the 256 px PNG if the ICO image plugin is unavailable.
    icon.addFile(QStringLiteral(":/icons/app.ico"));
    icon.addFile(QStringLiteral(":/icons/app.png"));
    if (icon.isNull())
        icon = IconFactory::icon(Glyph::Cube, QColor(74, 222, 128), 64);
    return icon;
}

} // namespace IconFactory
