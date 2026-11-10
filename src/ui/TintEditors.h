#pragma once

#include "core/TintRecipe.h"

#include <QColor>
#include <QVector>
#include <QWidget>
#include <functional>

namespace mcstudio {

class TintRampEditor : public QWidget
{
public:
    explicit TintRampEditor(QWidget *parent = nullptr);

    void setStops(const QVector<TintStop> &stops);
    QVector<TintStop> stops() const { return m_stops; }
    void setSelected(int index);
    int selected() const { return m_selected; }

    std::function<void()> onChanged;
    std::function<void(int)> onSelected;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRectF barRect() const;
    int hitStop(const QPoint &position) const;
    double positionAt(int x) const;
    void openColorEditor(int index);
    void sortStops();

    QVector<TintStop> m_stops;
    int m_selected = 0;
    bool m_dragging = false;
    int m_hover = -1;
};

class TintCurveEditor : public QWidget
{
public:
    explicit TintCurveEditor(QWidget *parent = nullptr);

    void setCurve(const QVector<TintCurvePoint> &curve);
    QVector<TintCurvePoint> curve() const { return m_curve; }

    std::function<void()> onChanged;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRectF plotRect() const;
    QPointF pointToWidget(const TintCurvePoint &point) const;
    TintCurvePoint widgetToPoint(const QPointF &position) const;
    int hitPoint(const QPoint &position) const;
    double evaluate(const QVector<TintCurvePoint> &points, double x) const;
    void sortCurve();

    QVector<TintCurvePoint> m_curve;
    int m_selected = -1;
    int m_hover = -1;
    bool m_dragging = false;
};

} // namespace mcstudio
