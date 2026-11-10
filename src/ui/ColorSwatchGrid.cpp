#include "ui/ColorSwatchGrid.h"

#include "ui/Theme.h"

#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

namespace mcstudio {
namespace {

constexpr int kCheckerCell = 4;

void drawChecker(QPainter &painter, const QRect &rect)
{
    const Theme::Colors colors = Theme::colors();
    painter.save();
    painter.setClipRect(rect);
    painter.fillRect(rect, colors.checkerA);
    for (int y = rect.top(); y < rect.bottom(); y += kCheckerCell) {
        for (int x = rect.left(); x < rect.right(); x += kCheckerCell) {
            const int cellX = (x - rect.left()) / kCheckerCell;
            const int cellY = (y - rect.top()) / kCheckerCell;
            if ((cellX + cellY) % 2 == 0)
                continue;
            painter.fillRect(QRect(x, y, kCheckerCell, kCheckerCell), colors.checkerB);
        }
    }
    painter.restore();
}

} // namespace

ColorSwatchGrid::ColorSwatchGrid(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void ColorSwatchGrid::setSwatches(const QVector<QColor> &colors, const QVector<int> &counts)
{
    m_colors = colors;
    m_counts = counts;
    m_hover = -1;
    setFixedHeight(preferredHeight());
    update();
}

void ColorSwatchGrid::setSelectedColor(const QColor &color)
{
    if (m_selected == color)
        return;
    m_selected = color;
    update();
}

void ColorSwatchGrid::setTooltipBuilder(
    std::function<QString(const QColor &, int)> builder)
{
    m_tooltipBuilder = std::move(builder);
}

int ColorSwatchGrid::preferredHeight() const
{
    const int columns = columnCount();
    const int rows = qMax(1, (m_colors.size() + columns - 1) / columns);
    return rows * m_swatchSize + (rows - 1) * m_gap;
}

QSize ColorSwatchGrid::sizeHint() const
{
    const int columns = qMin(columnCount(), qMax(1, m_colors.size()));
    return QSize(columns * m_swatchSize + (columns - 1) * m_gap, preferredHeight());
}

QSize ColorSwatchGrid::minimumSizeHint() const
{
    return sizeHint();
}

QRect ColorSwatchGrid::swatchRect(int index) const
{
    const int columns = columnCount();
    const int row = index / columns;
    const int column = index % columns;
    return QRect(column * (m_swatchSize + m_gap), row * (m_swatchSize + m_gap),
                 m_swatchSize, m_swatchSize);
}

int ColorSwatchGrid::indexAt(const QPoint &position) const
{
    const int columns = columnCount();
    if (columns <= 0)
        return -1;
    const int step = m_swatchSize + m_gap;
    if (step <= 0)
        return -1;
    const int column = position.x() / step;
    const int row = position.y() / step;
    if (column < 0 || column >= columns || row < 0)
        return -1;
    if ((position.x() % step) > m_swatchSize || (position.y() % step) > m_swatchSize)
        return -1;
    const int index = row * columns + column;
    return index >= 0 && index < m_colors.size() ? index : -1;
}

QString ColorSwatchGrid::tooltipFor(int index) const
{
    if (index < 0 || index >= m_colors.size())
        return {};
    const QColor color = m_colors.at(index);
    const int count = index < m_counts.size() ? m_counts.at(index) : 0;
    if (m_tooltipBuilder)
        return m_tooltipBuilder(color, count);

    QString text = color.name(QColor::HexRgb).toUpper();
    if (color.alpha() != 255)
        text += QStringLiteral(" · 透明度 %1%").arg(qRound(color.alphaF() * 100));
    if (count > 0)
        text += QStringLiteral(" · %1 像素").arg(count);
    return text;
}

void ColorSwatchGrid::paintEvent(QPaintEvent *)
{
    if (m_colors.isEmpty())
        return;

    const Theme::Colors colors = Theme::colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    for (int index = 0; index < m_colors.size(); ++index) {
        const QRect rect = swatchRect(index);
        if (!rect.intersects(this->rect()))
            continue;

        QPainterPath path;
        path.addRoundedRect(rect, 3, 3);
        painter.save();
        painter.setClipPath(path);
        drawChecker(painter, rect);
        painter.fillRect(rect, m_colors.at(index));
        painter.restore();

        const bool selected = m_colors.at(index) == m_selected;
        const bool hovered = index == m_hover;
        QPen pen(selected ? colors.accent
                          : (hovered ? colors.accentBorder : colors.borderStrong));
        pen.setWidth(selected ? 2 : 1);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(selected ? rect.adjusted(1, 1, -1, -1) : rect, 3, 3);
    }
}

void ColorSwatchGrid::mousePressEvent(QMouseEvent *event)
{
    const int index = indexAt(event->position().toPoint());
    if (index < 0) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (event->button() == Qt::LeftButton && onPick)
        onPick(m_colors.at(index));
    else if (event->button() == Qt::RightButton && onContextMenu)
        onContextMenu(m_colors.at(index), event->globalPosition().toPoint());
    event->accept();
}

void ColorSwatchGrid::mouseMoveEvent(QMouseEvent *event)
{
    const int index = indexAt(event->position().toPoint());
    if (index != m_hover) {
        m_hover = index;
        update();
        if (index >= 0) {
            const QRect rect = swatchRect(index);
            QToolTip::showText(mapToGlobal(rect.bottomLeft() + QPoint(0, 2)),
                               tooltipFor(index), this);
        } else {
            QToolTip::hideText();
        }
    }
    QWidget::mouseMoveEvent(event);
}

void ColorSwatchGrid::leaveEvent(QEvent *event)
{
    if (m_hover >= 0) {
        m_hover = -1;
        update();
    }
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}

void ColorSwatchGrid::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        update();
}

} // namespace mcstudio
