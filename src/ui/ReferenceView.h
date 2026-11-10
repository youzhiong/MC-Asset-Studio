#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QWidget>
#include <functional>

namespace mcstudio {

// Small reference viewer embedded in the editor side panel. It only displays
// another image (project texture, reference jar or a file); it never writes
// into the document. Drag to pan, wheel to zoom, click to pick a colour.
class ReferenceView : public QWidget
{
public:
    explicit ReferenceView(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void clearImage();
    bool hasImage() const { return !m_image.isNull(); }
    QImage image() const { return m_image; }
    void fitToView();
    void resetZoom();
    int zoomPercent() const;

    std::function<void(const QColor &)> onColorPicked;
    std::function<void()> onViewChanged;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QRectF imageRect() const;
    bool colorAt(const QPoint &position, QColor *color) const;
    void drawCheckerboard(QPainter &painter) const;

    QImage m_image;
    qreal m_scale = 1.0;
    QPointF m_offset;
    bool m_needsFit = true;
    bool m_dragging = false;
    bool m_moved = false;
    QPoint m_dragStart;
    QPointF m_offsetStart;
};

} // namespace mcstudio
