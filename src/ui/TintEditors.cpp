#include "ui/TintEditors.h"

#include "ui/Theme.h"

#include <QColorDialog>
#include <QEvent>
#include <QLineF>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>

#include <algorithm>
#include <cmath>

namespace mcstudio {
namespace {

constexpr double kEps = 1e-6;

QColor lerpColor(const QColor &a, const QColor &b, double t)
{
    const auto mix = [t](int from, int to) {
        return qBound(0, qRound(from + (to - from) * t), 255);
    };
    return QColor(mix(a.red(), b.red()), mix(a.green(), b.green()),
                  mix(a.blue(), b.blue()), mix(a.alpha(), b.alpha()));
}

} // namespace

TintRampEditor::TintRampEditor(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(58);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_stops = {{0.0, QColor(QStringLiteral("#1a1a1a"))},
               {0.5, QColor(QStringLiteral("#808080"))},
               {1.0, QColor(QStringLiteral("#ffffff"))}};
}

QSize TintRampEditor::sizeHint() const
{
    return QSize(280, 58);
}

QSize TintRampEditor::minimumSizeHint() const
{
    return QSize(180, 58);
}

void TintRampEditor::setStops(const QVector<TintStop> &stops)
{
    m_stops = stops;
    sortStops();
    m_selected = qBound(0, m_selected, qMax(0, m_stops.size() - 1));
    update();
}

void TintRampEditor::setSelected(int index)
{
    m_selected = qBound(0, index, qMax(0, m_stops.size() - 1));
    update();
    if (onSelected)
        onSelected(m_selected);
}

QRectF TintRampEditor::barRect() const
{
    return QRectF(10, 8, qMax(1, width() - 20), 26);
}

double TintRampEditor::positionAt(int x) const
{
    const QRectF bar = barRect();
    return qBound(0.0, (x - bar.left()) / qMax(1.0, bar.width()), 1.0);
}

void TintRampEditor::sortStops()
{
    if (m_stops.size() < 2) {
        if (m_stops.isEmpty())
            m_stops = {{0.0, Qt::black}, {1.0, Qt::white}};
        else
            m_stops.append({1.0, m_stops.first().color});
    }
    std::sort(m_stops.begin(), m_stops.end(),
              [](const TintStop &a, const TintStop &b) {
                  return a.position < b.position;
              });
}

int TintRampEditor::hitStop(const QPoint &position) const
{
    const QRectF bar = barRect();
    if (position.y() < bar.top() - 4 || position.y() > bar.bottom() + 18)
        return -1;
    for (int index = 0; index < m_stops.size(); ++index) {
        const double x = bar.left() + m_stops.at(index).position * bar.width();
        if (std::abs(position.x() - x) <= 7.0)
            return index;
    }
    return -1;
}

void TintRampEditor::paintEvent(QPaintEvent *)
{
    const Theme::Colors colors = Theme::colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bar = barRect();
    QLinearGradient gradient(bar.topLeft(), bar.topRight());
    for (const TintStop &stop : m_stops)
        gradient.setColorAt(qBound(0.0, stop.position, 1.0), stop.color);
    painter.setPen(QPen(colors.borderStrong, 1));
    painter.setBrush(gradient);
    painter.drawRoundedRect(bar, 3, 3);

    for (int index = 0; index < m_stops.size(); ++index) {
        const double x = bar.left() + m_stops.at(index).position * bar.width();
        const QPointF handle(x, bar.bottom() + 7);
        const bool selected = index == m_selected;
        const bool hovered = index == m_hover;
        painter.setPen(QPen(selected ? colors.accent
                                    : (hovered ? colors.accentBorder
                                               : colors.borderStrong),
                            selected ? 2 : 1));
        painter.setBrush(m_stops.at(index).color);
        painter.drawEllipse(handle, 5.0, 5.0);
        painter.setBrush(Qt::NoBrush);
        painter.drawLine(QPointF(x, bar.bottom() + 1), QPointF(x, bar.bottom() + 2));
    }

    if (!m_stops.isEmpty()) {
        painter.setPen(colors.textMuted);
        const QString text = QStringLiteral("%1%")
                                 .arg(qRound(m_stops.at(m_selected).position * 100));
        painter.drawText(QRectF(bar.left(), bar.bottom() + 12, bar.width(), 14),
                         Qt::AlignRight | Qt::AlignVCenter, text);
    }
}

void TintRampEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        const int index = hitStop(event->position().toPoint());
        if (index >= 0 && m_stops.size() > 2) {
            m_stops.removeAt(index);
            m_selected = qBound(0, index - 1, m_stops.size() - 1);
            if (onChanged)
                onChanged();
            update();
        }
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    int index = hitStop(event->position().toPoint());
    if (index < 0) {
        const double position = positionAt(event->position().toPoint().x());
        const QColor color = QColor::fromHsvF(position, 0.0, 1.0 - position * 0.85);
        m_stops.append({position, color});
        sortStops();
        index = 0;
        for (int i = 0; i < m_stops.size(); ++i) {
            if (std::abs(m_stops.at(i).position - position) < kEps) {
                index = i;
                break;
            }
        }
        if (onChanged)
            onChanged();
    }

    m_selected = index;
    m_dragging = true;
    update();
    if (onSelected)
        onSelected(m_selected);
    event->accept();
}

void TintRampEditor::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && !m_stops.isEmpty()) {
        const double position = positionAt(event->position().toPoint().x());
        const QColor color = m_stops.at(m_selected).color;
        TintStop moved{position, color};
        m_stops.removeAt(m_selected);
        m_stops.append(moved);
        std::sort(m_stops.begin(), m_stops.end(),
                  [](const TintStop &a, const TintStop &b) {
                      return a.position < b.position;
                  });
        for (int index = 0; index < m_stops.size(); ++index) {
            if (std::abs(m_stops.at(index).position - position) < kEps
                && m_stops.at(index).color == color) {
                m_selected = index;
                break;
            }
        }
        if (onChanged)
            onChanged();
        update();
        event->accept();
        return;
    }

    const int hover = hitStop(event->position().toPoint());
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void TintRampEditor::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TintRampEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int index = hitStop(event->position().toPoint());
    if (index >= 0) {
        openColorEditor(index);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void TintRampEditor::openColorEditor(int index)
{
    if (index < 0 || index >= m_stops.size())
        return;
    const QColor chosen = QColorDialog::getColor(m_stops.at(index).color, this,
                                                 tr("色标颜色"),
                                                 QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid())
        return;
    m_stops[index].color = chosen;
    if (onChanged)
        onChanged();
    update();
}

void TintRampEditor::leaveEvent(QEvent *event)
{
    if (m_hover >= 0) {
        m_hover = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

TintCurveEditor::TintCurveEditor(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(190);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_curve = {{0.0, 0.0}, {0.5, 0.5}, {1.0, 1.0}};
}

QSize TintCurveEditor::sizeHint() const
{
    return QSize(280, 200);
}

QSize TintCurveEditor::minimumSizeHint() const
{
    return QSize(200, 190);
}

void TintCurveEditor::setCurve(const QVector<TintCurvePoint> &curve)
{
    m_curve = curve;
    sortCurve();
    m_selected = -1;
    update();
}

void TintCurveEditor::sortCurve()
{
    if (m_curve.size() < 2) {
        m_curve = {{0.0, 0.0}, {1.0, 1.0}};
        return;
    }
    std::sort(m_curve.begin(), m_curve.end(),
              [](const TintCurvePoint &a, const TintCurvePoint &b) {
                  return a.x < b.x;
              });
    for (TintCurvePoint &point : m_curve) {
        point.x = qBound(0.0, point.x, 1.0);
        point.y = qBound(0.0, point.y, 1.0);
    }
    m_curve.first().x = 0.0;
    m_curve.last().x = 1.0;
}

QRectF TintCurveEditor::plotRect() const
{
    return QRectF(14, 12, qMax(1, width() - 28), qMax(1, height() - 30));
}

QPointF TintCurveEditor::pointToWidget(const TintCurvePoint &point) const
{
    const QRectF plot = plotRect();
    return QPointF(plot.left() + point.x * plot.width(),
                   plot.bottom() - point.y * plot.height());
}

TintCurvePoint TintCurveEditor::widgetToPoint(const QPointF &position) const
{
    const QRectF plot = plotRect();
    TintCurvePoint point;
    point.x = qBound(0.0, (position.x() - plot.left()) / qMax(1.0, plot.width()), 1.0);
    point.y = qBound(0.0, (plot.bottom() - position.y()) / qMax(1.0, plot.height()), 1.0);
    return point;
}

int TintCurveEditor::hitPoint(const QPoint &position) const
{
    for (int index = 0; index < m_curve.size(); ++index) {
        const QPointF widget = pointToWidget(m_curve.at(index));
        if (QLineF(widget, QPointF(position)).length() <= 8.0)
            return index;
    }
    return -1;
}

double TintCurveEditor::evaluate(const QVector<TintCurvePoint> &points, double x) const
{
    if (points.size() < 2)
        return x;
    const double value = qBound(0.0, x, 1.0);
    if (value <= points.first().x)
        return points.first().y;
    if (value >= points.last().x)
        return points.last().y;

    const int count = points.size();
    QVector<double> h(count - 1);
    QVector<double> delta(count - 1);
    for (int i = 0; i + 1 < count; ++i) {
        h[i] = qMax(kEps, points.at(i + 1).x - points.at(i).x);
        delta[i] = (points.at(i + 1).y - points.at(i).y) / h[i];
    }
    QVector<double> slope(count, 0.0);
    slope[0] = delta.first();
    slope[count - 1] = delta.last();
    for (int i = 1; i + 1 < count; ++i) {
        slope[i] = delta.at(i - 1) * delta.at(i) <= 0.0
                       ? 0.0
                       : (delta.at(i - 1) + delta.at(i)) / 2.0;
    }
    for (int i = 0; i + 1 < count; ++i) {
        if (std::abs(delta.at(i)) < kEps) {
            slope[i] = 0.0;
            slope[i + 1] = 0.0;
            continue;
        }
        const double alpha = slope.at(i) / delta.at(i);
        const double beta = slope.at(i + 1) / delta.at(i);
        const double magnitude = alpha * alpha + beta * beta;
        if (magnitude > 9.0) {
            const double scale = 3.0 / std::sqrt(magnitude);
            slope[i] = scale * alpha * delta.at(i);
            slope[i + 1] = scale * beta * delta.at(i);
        }
    }
    int segment = 0;
    for (int i = 0; i + 1 < count; ++i) {
        if (value >= points.at(i).x && value <= points.at(i + 1).x) {
            segment = i;
            break;
        }
    }
    const double t = (value - points.at(segment).x) / h.at(segment);
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double h00 = 2 * t3 - 3 * t2 + 1;
    const double h10 = t3 - 2 * t2 + t;
    const double h01 = -2 * t3 + 3 * t2;
    const double h11 = t3 - t2;
    return qBound(0.0, h00 * points.at(segment).y
                           + h10 * h.at(segment) * slope.at(segment)
                           + h01 * points.at(segment + 1).y
                           + h11 * h.at(segment) * slope.at(segment + 1),
                  1.0);
}

void TintCurveEditor::paintEvent(QPaintEvent *)
{
    const Theme::Colors colors = Theme::colors();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF plot = plotRect();
    painter.fillRect(plot, colors.base);
    painter.setPen(QPen(colors.border, 1));
    painter.drawRect(plot);

    painter.setPen(QPen(QColor(colors.border.red(), colors.border.green(),
                               colors.border.blue(), 120),
                        1, Qt::DashLine));
    for (int step = 1; step < 4; ++step) {
        const double t = step / 4.0;
        painter.drawLine(QPointF(plot.left() + t * plot.width(), plot.top()),
                         QPointF(plot.left() + t * plot.width(), plot.bottom()));
        painter.drawLine(QPointF(plot.left(), plot.top() + t * plot.height()),
                         QPointF(plot.right(), plot.top() + t * plot.height()));
    }
    painter.setPen(QPen(QColor(colors.textMuted.red(), colors.textMuted.green(),
                               colors.textMuted.blue(), 90),
                        1, Qt::DashLine));
    painter.drawLine(plot.bottomLeft(), plot.topRight());

    QPainterPath path;
    constexpr int steps = 128;
    for (int index = 0; index <= steps; ++index) {
        const double x = index / double(steps);
        const QPointF point = pointToWidget({x, evaluate(m_curve, x)});
        if (index == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    painter.setPen(QPen(colors.accent, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);

    for (int index = 0; index < m_curve.size(); ++index) {
        const QPointF widget = pointToWidget(m_curve.at(index));
        const bool selected = index == m_selected;
        const bool hovered = index == m_hover;
        painter.setPen(QPen(selected ? colors.accent
                                    : (hovered ? colors.accentBorder
                                               : colors.borderStrong),
                            selected ? 2 : 1));
        painter.setBrush(colors.panelAlt);
        painter.drawEllipse(widget, 4.5, 4.5);
    }

    painter.setPen(colors.textMuted);
    painter.drawText(QRectF(plot.left(), plot.bottom() + 2, 70, 14),
                     Qt::AlignLeft | Qt::AlignVCenter, tr("暗"));
    painter.drawText(QRectF(plot.right() - 60, plot.bottom() + 2, 60, 14),
                     Qt::AlignRight | Qt::AlignVCenter, tr("亮"));
}

void TintCurveEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        const int index = hitPoint(event->position().toPoint());
        if (index > 0 && index + 1 < m_curve.size()) {
            m_curve.removeAt(index);
            m_selected = -1;
            if (onChanged)
                onChanged();
            update();
        }
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    int index = hitPoint(event->position().toPoint());
    if (index < 0) {
        TintCurvePoint point = widgetToPoint(event->position());
        point.x = qBound(0.02, point.x, 0.98);
        m_curve.append(point);
        sortCurve();
        for (int i = 0; i < m_curve.size(); ++i) {
            if (std::abs(m_curve.at(i).x - point.x) < kEps) {
                index = i;
                break;
            }
        }
        if (onChanged)
            onChanged();
    }
    m_selected = index;
    m_dragging = true;
    update();
    event->accept();
}

void TintCurveEditor::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && m_selected >= 0 && m_selected < m_curve.size()) {
        TintCurvePoint point = widgetToPoint(event->position());
        if (m_selected == 0) {
            point.x = 0.0;
        } else if (m_selected == m_curve.size() - 1) {
            point.x = 1.0;
        } else {
            const double lower = m_curve.at(m_selected - 1).x + 0.01;
            const double upper = m_curve.at(m_selected + 1).x - 0.01;
            point.x = qBound(lower, point.x, upper);
        }
        m_curve[m_selected] = point;
        if (onChanged)
            onChanged();
        update();
        event->accept();
        return;
    }

    const int hover = hitPoint(event->position().toPoint());
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void TintCurveEditor::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void TintCurveEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
    QWidget::mouseDoubleClickEvent(event);
}

void TintCurveEditor::leaveEvent(QEvent *event)
{
    if (m_hover >= 0) {
        m_hover = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

} // namespace mcstudio
