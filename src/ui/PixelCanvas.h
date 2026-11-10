#pragma once

#include "core/PixelEdit.h"
#include "core/TextureDocument.h"

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QVector>
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
        Lasso,
        MagicWand,
    };

    // How the texture is repeated around the editable tile. Edits always apply
    // to the centre tile; every copy shows the same pixels live.
    enum class TileMode {
        None,
        Both,
        Horizontal,
        Vertical,
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

    void setTileMode(TileMode mode);
    TileMode tileMode() const { return m_tileMode; }

    // Reference image overlay: display-only, never saved into the document.
    void setReferenceImage(const QImage &image);
    void clearReferenceImage();
    bool hasReferenceImage() const { return !m_reference.isNull(); }
    void setReferenceVisible(bool visible);
    bool referenceVisible() const { return m_referenceVisible; }
    void setReferenceOpacity(int percent);
    int referenceOpacity() const { return m_referenceOpacity; }
    void setReferenceLocked(bool locked);
    bool referenceLocked() const { return m_referenceLocked; }
    void setReferenceAbove(bool above);
    bool referenceAbove() const { return m_referenceAbove; }
    void setReferenceAdjustMode(bool adjust);
    bool referenceAdjustMode() const { return m_referenceAdjust; }
    void setReferencePickMode(bool pick);
    bool referencePickMode() const { return m_referencePick; }
    void fitReferenceToView();
    void resetReferenceTransform();
    int referenceScalePercent() const;
    QString referenceSizeText() const;

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

    // Tint mask: a separate non-destructive selection used only by tint tools.
    // White pixels are selected; it never enters undo/save.
    bool hasTintMask() const { return !m_tintMask.isNull() && m_maskPixels > 0; }
    QImage tintMask() const { return m_tintMask; }
    int tintMaskPixels() const { return m_maskPixels; }
    void setTintMask(const QImage &mask);
    void clearTintMask();

    void refresh();

    std::function<void(QPoint, QColor)> onHover;
    std::function<void(QColor)> onColorPicked;
    std::function<void(QColor)> onReferenceColorPicked;
    std::function<void()> onReferenceChanged;
    std::function<void()> onEdited;
    std::function<void()> onSelectionChanged;
    std::function<void()> onMaskChanged;
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
    QRect tiledBounds() const;
    void drawTiledCopies(QPainter &painter, const QRect &bounds, const QImage &image) const;
    void drawReference(QPainter &painter) const;
    bool referenceColorAt(const QPoint &position, QColor *color) const;
    QPointF referenceCenter() const;
    void beginTintMask();
    void finishLassoMask();
    void applyWandMask(const QPoint &pixel);
    void rebuildMaskOverlay();
    void paintMaskOverlay(QPainter &painter) const;
    void paintLassoPreview(QPainter &painter) const;

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
    TileMode m_tileMode = TileMode::None;

    QImage m_reference;
    bool m_referenceVisible = false;
    int m_referenceOpacity = 60;
    bool m_referenceLocked = false;
    bool m_referenceAbove = true;
    bool m_referenceAdjust = false;
    bool m_referencePick = false;
    qreal m_referenceScale = 1.0;
    qreal m_referenceRotation = 0.0;
    QPointF m_referenceOffset;
    bool m_referenceDragging = false;
    QPoint m_referenceDragStart;
    QPointF m_referenceOffsetStart;

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

    QImage m_tintMask;
    QImage m_maskOverlay;
    int m_maskPixels = 0;
    QVector<QPoint> m_lassoPoints;
    bool m_lassoActive = false;
};

} // namespace mcstudio
