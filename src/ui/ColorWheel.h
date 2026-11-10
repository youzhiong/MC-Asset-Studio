#pragma once

#include <QColor>
#include <QImage>
#include <QWidget>
#include <functional>

namespace mcstudio {

// Classic HSV colour wheel: the angle picks the hue, the distance from the
// centre picks the saturation. Brightness comes from the page's own slider so
// the wheel itself stays readable. Cached as an image and rebuilt only on
// resize, so dragging stays smooth.
class ColorWheel : public QWidget
{
public:
    explicit ColorWheel(QWidget *parent = nullptr);

    void setColor(const QColor &color);
    QColor color() const;

    std::function<void(const QColor &)> onColorChanged;
    std::function<void()> onColorCommitted;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QRect wheelRect() const;
    QPoint center() const;
    int radius() const;
    void rebuildWheel();
    void updateFromPosition(const QPoint &position);

    int m_hue = 0;
    int m_saturation = 0;
    int m_value = 255;
    int m_alpha = 255;
    bool m_dragging = false;
    QImage m_wheel;
};

} // namespace mcstudio
