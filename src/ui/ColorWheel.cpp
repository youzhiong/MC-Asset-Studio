#include "ui/ColorWheel.h"

#include "ui/Theme.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

#include <cmath>

namespace mcstudio {
namespace {
constexpr qreal kPi = 3.14159265358979323846;
}

ColorWheel::ColorWheel(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize ColorWheel::sizeHint() const
{
    return QSize(196, 196);
}

QSize ColorWheel::minimumSizeHint() const
{
    return QSize(140, 140);
}

QRect ColorWheel::wheelRect() const
{
    const int side = qMin(width(), height());
    return QRect((width() - side) / 2, (height() - side) / 2, side, side);
}

QPoint ColorWheel::center() const
{
    const QRect rect = wheelRect();
    return QPoint(rect.center().x(), rect.center().y());
}

int ColorWheel::radius() const
{
    return qMax(1, wheelRect().width() / 2 - 4);
}

void ColorWheel::setColor(const QColor &color)
{
    if (!color.isValid())
        return;

    int hue = 0;
    int saturation = 0;
    int value = 0;
    int alpha = 0;
    color.getHsv(&hue, &saturation, &value, &alpha);
    if (hue >= 0)
        m_hue = hue;
    // Pure black reports saturation 0; keep the previous saturation so the
    // marker does not jump to the centre while brightness is at 0.
    if (value > 0)
        m_saturation = saturation;
    m_value = value;
    m_alpha = alpha;
    update();
}

QColor ColorWheel::color() const
{
    return QColor::fromHsv(qBound(0, m_hue, 359), qBound(0, m_saturation, 255),
                           qBound(0, m_value, 255), qBound(0, m_alpha, 255));
}

void ColorWheel::rebuildWheel()
{
    const int side = qMin(width(), height());
    if (side <= 8)
        return;

    const qreal dpr = devicePixelRatioF();
    const int pixels = qMax(16, qRound(side * dpr));
    QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    const qreal centerPixel = pixels / 2.0;
    const qreal outer = centerPixel - 4.0 * dpr;

    for (int y = 0; y < pixels; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        const qreal dy = y - centerPixel + 0.5;
        for (int x = 0; x < pixels; ++x) {
            const qreal dx = x - centerPixel + 0.5;
            const qreal distance = std::sqrt(dx * dx + dy * dy);
            if (distance > outer + 1.0 * dpr)
                continue;

            qreal hue = std::atan2(-dy, dx) * 180.0 / kPi;
            if (hue < 0)
                hue += 360.0;
            const qreal saturation = qMin(1.0, distance / outer);

            QColor color = QColor::fromHsvF(hue / 360.0, saturation, 1.0);
            qreal alpha = 1.0;
            if (distance > outer - 1.0 * dpr)
                alpha = qBound(0.0, (outer + 1.0 * dpr - distance) / (2.0 * dpr), 1.0);
            color.setAlphaF(alpha);
            row[x] = color.rgba();
        }
    }

    image.setDevicePixelRatio(dpr);
    m_wheel = image;
}

void ColorWheel::paintEvent(QPaintEvent *)
{
    const Theme::Colors colors = Theme::colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRect rect = wheelRect();
    if (m_wheel.isNull())
        rebuildWheel();
    if (!m_wheel.isNull())
        painter.drawImage(QPointF(rect.left(), rect.top()), m_wheel);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(colors.borderStrong, 1));
    painter.drawEllipse(QPointF(center()), radius() + 1.0, radius() + 1.0);

    // Current hue/saturation marker.
    const qreal angle = m_hue * kPi / 180.0;
    const qreal distance = radius() * (m_saturation / 255.0);
    const QPointF marker(center().x() + std::cos(angle) * distance,
                         center().y() - std::sin(angle) * distance);
    painter.setPen(QPen(QColor(0, 0, 0, 160), 3));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(marker, 5.0, 5.0);
    painter.setPen(QPen(Qt::white, 2));
    painter.drawEllipse(marker, 5.0, 5.0);
}

void ColorWheel::updateFromPosition(const QPoint &position)
{
    const QPoint offset = position - center();
    const qreal distance = std::hypot(qreal(offset.x()), qreal(offset.y()));
    qreal hue = std::atan2(-offset.y(), offset.x()) * 180.0 / kPi;
    if (hue < 0)
        hue += 360.0;
    m_hue = qBound(0, qRound(hue), 359);
    m_saturation = qRound(qMin(1.0, distance / qMax(1, radius())) * 255);
}

void ColorWheel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const QPoint offset = event->position().toPoint() - center();
    if (std::hypot(qreal(offset.x()), qreal(offset.y())) > radius() + 4.0) {
        event->accept();
        return;
    }

    m_dragging = true;
    updateFromPosition(event->position().toPoint());
    if (onColorChanged)
        onColorChanged(color());
    update();
    event->accept();
}

void ColorWheel::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    updateFromPosition(event->position().toPoint());
    if (onColorChanged)
        onColorChanged(color());
    update();
    event->accept();
}

void ColorWheel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        if (onColorCommitted)
            onColorCommitted();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ColorWheel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    rebuildWheel();
}

void ColorWheel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        update();
}

} // namespace mcstudio
