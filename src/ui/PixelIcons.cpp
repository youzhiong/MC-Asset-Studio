#include "ui/PixelIcons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

namespace PixelIcons {
namespace {

void prepare(QPainter &painter, const QColor &color, qreal width = 1.6)
{
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
}

} // namespace

QIcon icon(Glyph glyph, const QColor &color, int size)
{
    const int logical = qMax(12, size);
    QPixmap pixmap(logical * 2, logical * 2);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    prepare(painter, color);

    const qreal w = logical;
    const qreal h = logical;
    const auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) {
        painter.drawLine(QPointF(x1 * w, y1 * h), QPointF(x2 * w, y2 * h));
    };

    switch (glyph) {
    case Glyph::Pen: {
        QPolygonF body;
        body << QPointF(0.26 * w, 0.74 * h) << QPointF(0.32 * w, 0.54 * h)
             << QPointF(0.64 * w, 0.22 * h) << QPointF(0.78 * w, 0.36 * h)
             << QPointF(0.46 * w, 0.68 * h);
        painter.drawPolygon(body);
        line(0.32, 0.54, 0.46, 0.68);
        line(0.24, 0.78, 0.36, 0.76);
        break;
    }
    case Glyph::Eraser:
        painter.save();
        painter.translate(0.50 * w, 0.48 * h);
        painter.rotate(-36);
        painter.drawRoundedRect(QRectF(-0.28 * w, -0.12 * h, 0.56 * w, 0.24 * h), 2.0,
                                2.0);
        painter.restore();
        line(0.22, 0.78, 0.66, 0.78);
        break;
    case Glyph::Bucket: {
        QPainterPath path;
        path.moveTo(0.24 * w, 0.44 * h);
        path.lineTo(0.52 * w, 0.20 * h);
        path.lineTo(0.76 * w, 0.48 * h);
        path.lineTo(0.52 * w, 0.76 * h);
        path.closeSubpath();
        painter.drawPath(path);
        painter.drawArc(QRectF(0.26 * w, 0.12 * h, 0.34 * w, 0.26 * h), 20 * 16, 140 * 16);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(0.80 * w, 0.68 * h), 0.075 * w, 0.075 * w);
        break;
    }
    case Glyph::Dropper:
        line(0.26, 0.76, 0.56, 0.46);
        line(0.52, 0.34, 0.66, 0.48);
        painter.drawRoundedRect(QRectF(0.58 * w, 0.20 * h, 0.22 * w, 0.22 * h), 3.0, 3.0);
        painter.setBrush(color);
        painter.drawEllipse(QPointF(0.24 * w, 0.78 * h), 0.035 * w, 0.035 * h);
        break;
    case Glyph::Line:
        line(0.24, 0.76, 0.76, 0.24);
        painter.setBrush(color);
        painter.drawRect(QRectF(0.18 * w, 0.70 * h, 0.12 * w, 0.12 * h));
        painter.drawRect(QRectF(0.70 * w, 0.18 * h, 0.12 * w, 0.12 * h));
        break;
    case Glyph::Rect:
        painter.drawRect(QRectF(0.22 * w, 0.26 * h, 0.56 * w, 0.48 * h));
        break;
    case Glyph::Ellipse:
        painter.drawEllipse(QRectF(0.20 * w, 0.26 * h, 0.60 * w, 0.48 * h));
        break;
    case Glyph::Select: {
        QPen pen = painter.pen();
        pen.setStyle(Qt::DashLine);
        pen.setDashPattern({2.0, 1.5});
        painter.setPen(pen);
        painter.drawRect(QRectF(0.22 * w, 0.26 * h, 0.56 * w, 0.48 * h));
        break;
    }
    case Glyph::Undo:
    case Glyph::Redo: {
        const bool redo = glyph == Glyph::Redo;
        painter.save();
        if (redo) {
            painter.translate(w, 0);
            painter.scale(-1, 1);
        }
        painter.drawArc(QRectF(0.24 * w, 0.30 * h, 0.52 * w, 0.44 * h), 30 * 16,
                        210 * 16);
        QPolygonF arrow;
        arrow << QPointF(0.24 * w, 0.30 * h) << QPointF(0.24 * w, 0.52 * h)
              << QPointF(0.40 * w, 0.40 * h);
        painter.setBrush(color);
        painter.drawPolygon(arrow);
        painter.restore();
        break;
    }
    case Glyph::Grid:
        line(0.50, 0.18, 0.50, 0.82);
        line(0.18, 0.50, 0.82, 0.50);
        painter.drawRect(QRectF(0.18 * w, 0.18 * h, 0.64 * w, 0.64 * h));
        break;
    case Glyph::Symmetry: {
        QPen pen = painter.pen();
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        line(0.50, 0.14, 0.50, 0.86);
        prepare(painter, color);
        QPolygonF left;
        left << QPointF(0.40 * w, 0.32 * h) << QPointF(0.20 * w, 0.50 * h)
             << QPointF(0.40 * w, 0.68 * h);
        painter.drawPolygon(left);
        QPolygonF right;
        right << QPointF(0.60 * w, 0.32 * h) << QPointF(0.80 * w, 0.50 * h)
              << QPointF(0.60 * w, 0.68 * h);
        painter.drawPolygon(right);
        break;
    }
    case Glyph::Fit:
        line(0.18, 0.36, 0.18, 0.18);
        line(0.18, 0.18, 0.36, 0.18);
        line(0.64, 0.18, 0.82, 0.18);
        line(0.82, 0.18, 0.82, 0.36);
        line(0.82, 0.64, 0.82, 0.82);
        line(0.82, 0.82, 0.64, 0.82);
        line(0.36, 0.82, 0.18, 0.82);
        line(0.18, 0.82, 0.18, 0.64);
        break;
    }

    painter.end();

    QIcon result;
    result.addPixmap(pixmap, QIcon::Normal);
    return result;
}

} // namespace PixelIcons
