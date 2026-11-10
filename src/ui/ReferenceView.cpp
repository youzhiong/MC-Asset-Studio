#include "ui/ReferenceView.h"

#include "ui/Theme.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QWheelEvent>
#include <QtMath>

namespace mcstudio {

ReferenceView::ReferenceView(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setMinimumHeight(180);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize ReferenceView::sizeHint() const
{
    return QSize(260, 200);
}

QSize ReferenceView::minimumSizeHint() const
{
    return QSize(180, 160);
}

void ReferenceView::setImage(const QImage &image)
{
    m_image = image;
    m_needsFit = true;
    m_offset = QPointF();
    m_scale = 1.0;
    if (!m_image.isNull())
        fitToView();
    update();
    if (onViewChanged)
        onViewChanged();
}

void ReferenceView::clearImage()
{
    m_image = QImage();
    m_needsFit = true;
    m_scale = 1.0;
    m_offset = QPointF();
    update();
    if (onViewChanged)
        onViewChanged();
}

void ReferenceView::fitToView()
{
    if (m_image.isNull()) {
        m_needsFit = true;
        return;
    }
    if (width() < 16 || height() < 16) {
        m_needsFit = true;
        return;
    }

    const qreal availableWidth = qMax(16, width() - 20);
    const qreal availableHeight = qMax(16, height() - 20);
    m_scale = qMin(availableWidth / qreal(m_image.width()),
                   availableHeight / qreal(m_image.height()));
    m_scale = qBound(0.02, m_scale, 32.0);
    m_offset = QPointF();
    m_needsFit = false;
    update();
    if (onViewChanged)
        onViewChanged();
}

void ReferenceView::resetZoom()
{
    m_scale = 1.0;
    m_offset = QPointF();
    update();
    if (onViewChanged)
        onViewChanged();
}

int ReferenceView::zoomPercent() const
{
    return qRound(m_scale * 100.0);
}

QRectF ReferenceView::imageRect() const
{
    if (m_image.isNull())
        return {};
    const QSizeF size(m_image.width() * m_scale, m_image.height() * m_scale);
    const QPointF center(width() / 2.0, height() / 2.0);
    return QRectF(center + m_offset - QPointF(size.width() / 2.0, size.height() / 2.0),
                  size);
}

void ReferenceView::drawCheckerboard(QPainter &painter) const
{
    const Theme::Colors colors = Theme::colors();
    painter.fillRect(rect(), colors.checkerA);
    constexpr int cell = 8;
    for (int y = 0; y < height(); y += cell) {
        for (int x = 0; x < width(); x += cell) {
            if (((x / cell) + (y / cell)) % 2 == 0)
                continue;
            painter.fillRect(QRect(x, y, cell, cell), colors.checkerB);
        }
    }
}

void ReferenceView::paintEvent(QPaintEvent *)
{
    const Theme::Colors colors = Theme::colors();
    QPainter painter(this);
    drawCheckerboard(painter);

    if (m_image.isNull()) {
        painter.setPen(colors.textMuted);
        painter.drawText(rect().adjusted(8, 8, -8, -8),
                         Qt::AlignCenter | Qt::TextWordWrap,
                         tr("还没有参考图\n可以在下面选择项目材质、参考实例材质或电脑图片"));
        painter.setPen(QPen(colors.borderStrong, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect().adjusted(0, 0, -1, -1));
        return;
    }

    const QRectF target = imageRect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_scale < 1.0);
    painter.drawImage(target, m_image);

    painter.setPen(QPen(colors.borderStrong, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(target.adjusted(0, 0, -1, -1));

    painter.setPen(QPen(colors.borderStrong, 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

bool ReferenceView::colorAt(const QPoint &position, QColor *color) const
{
    if (m_image.isNull())
        return false;
    const QRectF target = imageRect();
    if (!target.contains(QPointF(position)))
        return false;

    const qreal x = (position.x() - target.left()) / m_scale;
    const qreal y = (position.y() - target.top()) / m_scale;
    const int px = qBound(0, int(x), m_image.width() - 1);
    const int py = qBound(0, int(y), m_image.height() - 1);
    const QColor sampled = m_image.pixelColor(px, py);
    if (!sampled.isValid() || sampled.alpha() == 0)
        return false;
    if (color)
        *color = sampled;
    return true;
}

void ReferenceView::mousePressEvent(QMouseEvent *event)
{
    if (m_image.isNull()) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (event->button() == Qt::RightButton) {
        QColor color;
        if (colorAt(event->position().toPoint(), &color) && onColorPicked)
            onColorPicked(color);
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_moved = false;
        m_dragStart = event->position().toPoint();
        m_offsetStart = m_offset;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void ReferenceView::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint delta = event->position().toPoint() - m_dragStart;
    if (delta.manhattanLength() > 3)
        m_moved = true;
    m_offset = m_offsetStart + QPointF(delta);
    update();
    if (onViewChanged)
        onViewChanged();
    event->accept();
}

void ReferenceView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        if (!m_moved) {
            QColor color;
            if (colorAt(event->position().toPoint(), &color) && onColorPicked)
                onColorPicked(color);
        }
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void ReferenceView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        fitToView();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void ReferenceView::wheelEvent(QWheelEvent *event)
{
    if (m_image.isNull()) {
        QWidget::wheelEvent(event);
        return;
    }

    const int steps = event->angleDelta().y() > 0 ? 1 : -1;
    const qreal factor = steps > 0 ? 1.15 : 1.0 / 1.15;
    m_scale = qBound(0.02, m_scale * factor, 32.0);
    update();
    if (onViewChanged)
        onViewChanged();
    event->accept();
}

void ReferenceView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_needsFit && !m_image.isNull())
        fitToView();
}

void ReferenceView::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        update();
}

} // namespace mcstudio
