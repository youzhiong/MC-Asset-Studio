#pragma once

#include <QColor>
#include <QVector>
#include <QWidget>
#include <functional>

namespace mcstudio {

// A compact, custom-painted grid of colour swatches. It draws the alpha channel
// over a checkerboard (so half-transparent colours are readable), highlights the
// current colour, and reports left clicks / context menus for the owner page.
class ColorSwatchGrid : public QWidget
{
public:
    explicit ColorSwatchGrid(QWidget *parent = nullptr);

    void setSwatches(const QVector<QColor> &colors, const QVector<int> &counts = {});
    void setSelectedColor(const QColor &color);
    void setTooltipBuilder(std::function<QString(const QColor &, int)> builder);

    QVector<QColor> swatches() const { return m_colors; }
    int preferredHeight() const;

    std::function<void(const QColor &)> onPick;
    std::function<void(const QColor &, const QPoint &)> onContextMenu;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    int columnCount() const { return 8; }
    QRect swatchRect(int index) const;
    int indexAt(const QPoint &position) const;
    QString tooltipFor(int index) const;

    QVector<QColor> m_colors;
    QVector<int> m_counts;
    QColor m_selected;
    int m_swatchSize = 22;
    int m_gap = 3;
    int m_hover = -1;
    std::function<QString(const QColor &, int)> m_tooltipBuilder;
};

} // namespace mcstudio
