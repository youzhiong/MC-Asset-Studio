#pragma once

#include "core/PixelEdit.h"
#include "core/TextureDocument.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QWidget>
#include <functional>

namespace mcstudio {

class PixelCanvas : public QWidget
{
public:
    enum class Tool {
        Pen,
        Eraser,
        Bucket,
        Eyedropper,
        Line,
        Rect,
        Ellipse,
        Select,
    };

    explicit PixelCanvas(QWidget *parent = nullptr);

    void setDocument(TextureDocument *document);
    TextureDocument *document() const { return m_document; }

    void setFrame(int entry);
    int frame() const { return m_frame; }

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }

    void setColor(const QColor &color);
    QColor color() const { return m_color; }

    void setBrushSize(int size);
    int brushSize() const { return m_brushSize; }

    void setBucketContiguous(bool contiguous) { m_bucketContiguous = contiguous; }
    bool bucketContiguous() const { return m_bucketContiguous; }
    void setBucketTolerance(int tolerance) { m_bucketTolerance = qBound(0, tolerance, 255); }
    int bucketTolerance() const { return m_bucketTolerance; }

    void setShapeFill(bool fill) { m_shapeFill = fill; }
    bool shapeFill() const { return m_shapeFill; }

    void setShowGrid(bool show);
    bool showGrid() const { return m_showGrid; }

    void setSymmetry(PixelEdit::Symmetry symmetry);
    PixelEdit::Symmetry symmetry() const { return m_symmetry; }

    void setOnionSkin(bool enabled);
    bool onionSkin() const { return m_onionSkin; }

    void setZoomPercent(int percent);
    int zoomPercent() const { return m_zoom * 100; }
    void zoomIn();
    void zoomOut();
    void fitToView();

    void clearSelection();
    bool hasSelection() const { return !m_selection.isEmpty(); }
    QRect selection() const { return m_selection; }
    QImage selectionImage() const;
    bool pasteImage(const QImage &image);
    bool deleteSelection();
    bool copySelectionToClipboard();
    bool pasteFromClipboard();

    void refresh();

    std::function<void(QPoint, QColor)> onHover;
    std::function<void(QColor)> onColorPicked;
    std::function<void()> onEdited;
    std::function<void()> onSelectionChanged;
    std::function<void(int)> onZoomChanged;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QRect imageRect() const;
    QPoint pixelAt(const QPoint &position) const;
    bool pixelValid(const QPoint &pixel) const;
    QPoint framePointForMode(const QPoint &point, int mode) const;

    void fitToViewport();
    void updateHover(const QPoint &position);
    void pickColor(const QPoint &pixel);
    void beginDraw(const QPoint &pixel);
    void continueDraw(const QPoint &pixel);
    void finishDraw();
    void drawSymmetricPoint(QImage &image, const QPoint &point, const QColor &color);
    void drawSymmetricLine(QImage &image, const QPoint &a, const QPoint &b,
                           const QColor &color);
    void drawShapePreview();
    void applyMovingSelection();
    void drawCheckerboard(QPainter &painter, const QRect &rect) const;

    TextureDocument *m_document = nullptr;
    int m_frame = 0;

    Tool m_tool = Tool::Pen;
    QColor m_color = QColor(QStringLiteral("#f9fffe"));
    int m_brushSize = 1;
    bool m_bucketContiguous = true;
    int m_bucketTolerance = 0;
    bool m_shapeFill = false;
    bool m_showGrid = false;
    PixelEdit::Symmetry m_symmetry = PixelEdit::Symmetry::None;
    bool m_onionSkin = false;

    int m_zoom = 1;
    QPointF m_pan;
    bool m_needsFit = true;
    bool m_spacePressed = false;
    bool m_panning = false;
    QPoint m_lastPan;
    QPoint m_hover;
    bool m_hasHover = false;

    bool m_drawing = false;
    QPoint m_lastPixel;
    QPoint m_shapeStart;
    QPoint m_shapeEnd;
    QImage m_previewImage;
    bool m_shapeActive = false;

    bool m_selecting = false;
    QPoint m_selectStart;
    QPoint m_selectEnd;
    QRect m_selection;
    bool m_movingSelection = false;
    QPoint m_moveStart;
    QPoint m_moveOffset;
    QImage m_floatImage;
    QImage m_floatBase;
    QRect m_floatSelection;
};

} // namespace mcstudio
