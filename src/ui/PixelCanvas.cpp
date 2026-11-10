#include "ui/PixelCanvas.h"

#include "ui/Theme.h"

#include <QApplication>
#include <QBrush>
#include <QClipboard>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QPolygonF>
#include <QRectF>
#include <QResizeEvent>
#include <QTransform>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>

namespace mcstudio {

PixelCanvas::PixelCanvas(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("PixelCanvas"));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setMinimumSize(320, 260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PixelCanvas::setDocument(TextureDocument *document)
{
    m_document = document;
    m_frame = 0;
    m_needsFit = true;
    m_pan = QPointF();
    clearSelection();
    clearTintMask();
    m_previewImage = QImage();
    m_floatImage = QImage();
    m_floatBase = QImage();
    update();
}

void PixelCanvas::setFrame(int entry)
{
    if (!m_document)
        return;
    m_frame = qBound(0, entry, qMax(0, m_document->frameCount() - 1));
    clearSelection();
    m_previewImage = QImage();
    m_movingSelection = false;
    update();
}

void PixelCanvas::setTool(Tool tool)
{
    m_tool = tool;
    m_shapeActive = false;
    m_previewImage = QImage();
    if (tool != Tool::Select)
        clearSelection();
    update();
}

void PixelCanvas::setColor(const QColor &color)
{
    m_color = color;
}

void PixelCanvas::setBrushSize(int size)
{
    m_brushSize = qBound(1, size, 64);
}

void PixelCanvas::setShowGrid(bool show)
{
    m_showGrid = show;
    update();
}

void PixelCanvas::setSymmetry(PixelEdit::Symmetry symmetry)
{
    m_symmetry = symmetry;
    update();
}

void PixelCanvas::setOnionSkin(bool enabled)
{
    m_onionSkin = enabled;
    update();
}

void PixelCanvas::setTileMode(TileMode mode)
{
    if (m_tileMode == mode)
        return;
    m_tileMode = mode;
    update();
}

void PixelCanvas::setReferenceImage(const QImage &image)
{
    m_reference = image;
    m_referenceVisible = !image.isNull();
    m_referenceAdjust = false;
    m_referenceDragging = false;
    if (!m_reference.isNull())
        fitReferenceToView();
    update();
}

void PixelCanvas::clearReferenceImage()
{
    m_reference = QImage();
    m_referenceVisible = false;
    m_referenceAdjust = false;
    m_referencePick = false;
    m_referenceDragging = false;
    setCursor(Qt::CrossCursor);
    update();
}

void PixelCanvas::setReferenceVisible(bool visible)
{
    m_referenceVisible = visible && !m_reference.isNull();
    update();
}

void PixelCanvas::setReferenceOpacity(int percent)
{
    m_referenceOpacity = qBound(0, percent, 100);
    update();
}

void PixelCanvas::setReferenceLocked(bool locked)
{
    m_referenceLocked = locked;
}

void PixelCanvas::setReferenceAbove(bool above)
{
    m_referenceAbove = above;
    update();
}

void PixelCanvas::setReferenceAdjustMode(bool adjust)
{
    m_referenceAdjust = adjust;
    if (adjust)
        setCursor(Qt::OpenHandCursor);
    else if (!m_referencePick)
        setCursor(Qt::CrossCursor);
    update();
}

void PixelCanvas::setReferencePickMode(bool pick)
{
    m_referencePick = pick;
    if (pick)
        setCursor(Qt::CrossCursor);
    else if (m_referenceAdjust)
        setCursor(Qt::OpenHandCursor);
    else
        setCursor(Qt::CrossCursor);
    update();
}

void PixelCanvas::fitReferenceToView()
{
    if (m_reference.isNull())
        return;

    QRect target = imageRect();
    if (target.isEmpty())
        target = rect();
    const qreal availableWidth = qMax(32, target.width() - 16);
    const qreal availableHeight = qMax(32, target.height() - 16);
    const qreal fit = qMin(availableWidth / qreal(m_reference.width()),
                           availableHeight / qreal(m_reference.height()));
    m_referenceScale = qBound(0.05, fit, 8.0);
    m_referenceRotation = 0.0;
    m_referenceOffset =
        QPointF(target.center().x() - width() / 2.0, target.center().y() - height() / 2.0);
    update();
    if (onReferenceChanged)
        onReferenceChanged();
}

void PixelCanvas::resetReferenceTransform()
{
    m_referenceScale = 1.0;
    m_referenceRotation = 0.0;
    m_referenceOffset = QPointF();
    update();
    if (onReferenceChanged)
        onReferenceChanged();
}

int PixelCanvas::referenceScalePercent() const
{
    return qRound(m_referenceScale * 100.0);
}

QString PixelCanvas::referenceSizeText() const
{
    if (m_reference.isNull())
        return {};
    return QStringLiteral("%1 × %2").arg(m_reference.width()).arg(m_reference.height());
}

QPointF PixelCanvas::referenceCenter() const
{
    return QPointF(width() / 2.0, height() / 2.0) + m_referenceOffset;
}

void PixelCanvas::drawReference(QPainter &painter) const
{
    if (m_reference.isNull() || !m_referenceVisible)
        return;

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_referenceScale < 1.0);
    painter.setOpacity(qBound(0, m_referenceOpacity, 100) / 100.0);
    painter.translate(referenceCenter());
    painter.rotate(m_referenceRotation);
    painter.scale(m_referenceScale, m_referenceScale);
    painter.translate(-m_reference.width() / 2.0, -m_reference.height() / 2.0);
    painter.drawImage(QPointF(0, 0), m_reference);
    painter.restore();

    if (!m_referenceAdjust && !m_referencePick)
        return;

    QTransform transform;
    transform.translate(referenceCenter().x(), referenceCenter().y());
    transform.rotate(m_referenceRotation);
    transform.scale(m_referenceScale, m_referenceScale);
    transform.translate(-m_reference.width() / 2.0, -m_reference.height() / 2.0);

    QPolygonF outline;
    outline << transform.map(QPointF(0, 0))
            << transform.map(QPointF(m_reference.width(), 0))
            << transform.map(QPointF(m_reference.width(), m_reference.height()))
            << transform.map(QPointF(0, m_reference.height()));

    const Theme::Colors colors = Theme::colors();
    QPen pen(colors.accent);
    pen.setStyle(Qt::DashLine);
    pen.setWidth(1);
    painter.setOpacity(1.0);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(outline);

    painter.setPen(colors.accentText);
    painter.drawText(outline.boundingRect().topLeft() + QPointF(3, 14),
                     tr("参考图"));
}

bool PixelCanvas::referenceColorAt(const QPoint &position, QColor *color) const
{
    if (m_reference.isNull() || !m_referenceVisible)
        return false;

    QTransform transform;
    transform.translate(referenceCenter().x(), referenceCenter().y());
    transform.rotate(m_referenceRotation);
    transform.scale(m_referenceScale, m_referenceScale);
    transform.translate(-m_reference.width() / 2.0, -m_reference.height() / 2.0);

    bool invertible = false;
    const QTransform inverse = transform.inverted(&invertible);
    if (!invertible)
        return false;

    const QPointF imagePoint = inverse.map(QPointF(position));
    const int x = qFloor(imagePoint.x());
    const int y = qFloor(imagePoint.y());
    if (x < 0 || y < 0 || x >= m_reference.width() || y >= m_reference.height())
        return false;

    const QColor sampled = m_reference.pixelColor(x, y);
    if (!sampled.isValid() || sampled.alpha() == 0)
        return false;
    if (color)
        *color = sampled;
    return true;
}

void PixelCanvas::setZoomPercent(int percent)
{
    const int zoom = qBound(1, percent / 100, 64);
    if (m_zoom == zoom)
        return;
    m_zoom = zoom;
    m_needsFit = false;
    update();
    if (onZoomChanged)
        onZoomChanged(zoomPercent());
}

void PixelCanvas::zoomIn()
{
    const int next = qBound(1, m_zoom + (m_zoom < 8 ? 1 : 2), 64);
    if (next == m_zoom)
        return;
    m_zoom = next;
    m_needsFit = false;
    update();
    if (onZoomChanged)
        onZoomChanged(zoomPercent());
}

void PixelCanvas::zoomOut()
{
    const int next = qBound(1, m_zoom - (m_zoom <= 8 ? 1 : 2), 64);
    if (next == m_zoom)
        return;
    m_zoom = next;
    m_needsFit = false;
    update();
    if (onZoomChanged)
        onZoomChanged(zoomPercent());
}

void PixelCanvas::fitToView()
{
    fitToViewport();
}

void PixelCanvas::refresh()
{
    if (m_document && m_document->isValid()) {
        m_frame = qBound(0, m_frame, qMax(0, m_document->frameCount() - 1));
        if (!m_selection.isEmpty()) {
            const QRect bounds(QPoint(0, 0), m_document->frameSize());
            m_selection = m_selection.intersected(bounds);
        }
        if (!m_tintMask.isNull()
            && m_tintMask.size() != m_document->frameSize()) {
            clearTintMask();
        }
    }
    update();
}

void PixelCanvas::clearSelection()
{
    if (m_selection.isEmpty())
        return;
    m_selection = QRect();
    m_selecting = false;
    if (onSelectionChanged)
        onSelectionChanged();
    update();
}

void PixelCanvas::setTintMask(const QImage &mask)
{
    m_tintMask = mask;
    m_lassoActive = false;
    m_lassoPoints.clear();
    if (m_tintMask.isNull()) {
        m_maskOverlay = QImage();
        m_maskPixels = 0;
    } else {
        rebuildMaskOverlay();
    }
    update();
}

void PixelCanvas::clearTintMask()
{    if (m_tintMask.isNull() && m_maskOverlay.isNull() && !m_lassoActive)
        return;
    m_tintMask = QImage();
    m_maskOverlay = QImage();
    m_maskPixels = 0;
    m_lassoPoints.clear();
    m_lassoActive = false;
    update();
    if (onMaskChanged)
        onMaskChanged();
}

void PixelCanvas::beginTintMask()
{
    if (!m_document || !m_document->isValid())
        return;
    m_tintMask = QImage(m_document->frameSize(), QImage::Format_ARGB32);
    m_tintMask.fill(Qt::black);
    m_maskOverlay = QImage();
    m_maskPixels = 0;
}

void PixelCanvas::rebuildMaskOverlay()
{
    if (m_tintMask.isNull()) {
        m_maskOverlay = QImage();
        m_maskPixels = 0;
        return;
    }

    const QColor accent(80, 170, 255);
    m_maskOverlay = QImage(m_tintMask.size(), QImage::Format_ARGB32);
    m_maskOverlay.fill(Qt::transparent);
    int count = 0;
    for (int y = 0; y < m_tintMask.height(); ++y) {
        const QRgb *source =
            reinterpret_cast<const QRgb *>(m_tintMask.constScanLine(y));
        QRgb *target = reinterpret_cast<QRgb *>(m_maskOverlay.scanLine(y));
        for (int x = 0; x < m_tintMask.width(); ++x) {
            if (qRed(source[x]) < 128)
                continue;
            target[x] = qRgba(accent.red(), accent.green(), accent.blue(), 70);
            ++count;
        }
    }
    m_maskPixels = count;
    if (onMaskChanged)
        onMaskChanged();
}

void PixelCanvas::finishLassoMask()
{
    if (!m_lassoActive)
        return;
    m_lassoActive = false;
    if (m_lassoPoints.size() < 3 || !m_document) {
        m_lassoPoints.clear();
        update();
        return;
    }

    beginTintMask();
    if (!m_tintMask.isNull()) {
        QPainter painter(&m_tintMask);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawPolygon(QPolygon(m_lassoPoints));
        painter.end();
        rebuildMaskOverlay();
    }
    m_lassoPoints.clear();
    update();
}

void PixelCanvas::applyWandMask(const QPoint &pixel)
{
    if (!m_document || !m_document->isValid() || !pixelValid(pixel))
        return;

    const QImage frame = m_document->frame(m_frame);
    const int width = frame.width();
    const int height = frame.height();
    beginTintMask();
    if (m_tintMask.isNull())
        return;

    const QRgb target = frame.pixel(pixel);
    const int tolerance = m_bucketTolerance;
    const auto same = [tolerance](QRgb a, QRgb b) {
        if (tolerance <= 0)
            return a == b;
        return qAbs(qRed(a) - qRed(b)) <= tolerance
               && qAbs(qGreen(a) - qGreen(b)) <= tolerance
               && qAbs(qBlue(a) - qBlue(b)) <= tolerance
               && qAbs(qAlpha(a) - qAlpha(b)) <= tolerance;
    };

    QVector<quint8> visited(width * height, 0);
    QVector<QPoint> stack;
    stack.append(pixel);
    while (!stack.isEmpty()) {
        const QPoint point = stack.takeLast();
        const int index = point.y() * width + point.x();
        if (visited[index])
            continue;
        visited[index] = 1;
        if (!same(frame.pixel(point), target))
            continue;

        reinterpret_cast<QRgb *>(m_tintMask.scanLine(point.y()))[point.x()] =
            qRgb(255, 255, 255);

        if (point.x() > 0)
            stack.append(QPoint(point.x() - 1, point.y()));
        if (point.x() + 1 < width)
            stack.append(QPoint(point.x() + 1, point.y()));
        if (point.y() > 0)
            stack.append(QPoint(point.x(), point.y() - 1));
        if (point.y() + 1 < height)
            stack.append(QPoint(point.x(), point.y() + 1));
    }
    rebuildMaskOverlay();
    update();
}

void PixelCanvas::paintMaskOverlay(QPainter &painter) const
{
    if (m_maskOverlay.isNull() || m_tintMask.isNull())
        return;
    painter.drawImage(imageRect(), m_maskOverlay);
}

void PixelCanvas::paintLassoPreview(QPainter &painter) const
{
    if (!m_lassoActive || m_lassoPoints.size() < 2)
        return;
    const QRect bounds = imageRect();
    if (bounds.isEmpty())
        return;

    QPolygon screenPoints;
    for (const QPoint &point : m_lassoPoints) {
        screenPoints << QPoint(bounds.left() + point.x() * m_zoom,
                               bounds.top() + point.y() * m_zoom);
    }
    const Theme::Colors colors = Theme::colors();
    QPen pen(colors.accent);
    pen.setStyle(Qt::DashLine);
    pen.setWidth(1);
    painter.setPen(pen);
    painter.setBrush(QColor(colors.accent.red(), colors.accent.green(),
                            colors.accent.blue(), 28));
    painter.drawPolygon(screenPoints);
    painter.setBrush(Qt::NoBrush);
}

QImage PixelCanvas::selectionImage() const
{
    if (!m_document || !m_document->isValid() || m_selection.isEmpty())
        return {};
    return m_document->frame(m_frame).copy(m_selection);
}

bool PixelCanvas::pasteImage(const QImage &image)
{
    if (!m_document || !m_document->isValid() || image.isNull())
        return false;

    const QSize frameSize = m_document->frameSize();
    QPoint target;
    if (!m_selection.isEmpty()) {
        target = m_selection.topLeft();
    } else {
        // Paste where the mouse is looking; fall back to the visible centre.
        QPoint anchor = m_hasHover ? m_hover : pixelAt(rect().center());
        if (!pixelValid(anchor))
            anchor = QPoint(frameSize.width() / 2, frameSize.height() / 2);
        target = anchor - QPoint(image.width() / 2, image.height() / 2);
    }
    target.setX(qBound(0, target.x(), qMax(0, frameSize.width() - image.width())));
    target.setY(qBound(0, target.y(), qMax(0, frameSize.height() - image.height())));

    QImage &frame = m_document->frameRef(m_frame);
    QPainter painter(&frame);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(target, image);
    painter.end();

    m_selection = QRect(target, image.size()).intersected(QRect(QPoint(0, 0), frameSize));
    m_document->commitEdit();
    if (onSelectionChanged)
        onSelectionChanged();
    if (onEdited)
        onEdited();
    update();
    return true;
}

bool PixelCanvas::deleteSelection()
{
    if (!m_document || !m_document->isValid() || m_selection.isEmpty())
        return false;

    QImage &frame = m_document->frameRef(m_frame);
    QPainter painter(&frame);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(m_selection, Qt::transparent);
    painter.end();

    m_document->commitEdit();
    if (onEdited)
        onEdited();
    update();
    return true;
}

bool PixelCanvas::copySelectionToClipboard()
{
    const QImage image = selectionImage();
    if (image.isNull())
        return false;
    QApplication::clipboard()->setImage(image);
    return true;
}

bool PixelCanvas::pasteFromClipboard()
{
    return pasteImage(QApplication::clipboard()->image());
}

QRect PixelCanvas::imageRect() const
{
    if (!m_document || !m_document->isValid())
        return {};
    const QSize frame = m_document->frameSize();
    if (frame.isEmpty())
        return {};
    const QSize scaled(frame.width() * m_zoom, frame.height() * m_zoom);
    const QPoint topLeft((width() - scaled.width()) / 2 + qRound(m_pan.x()),
                         (height() - scaled.height()) / 2 + qRound(m_pan.y()));
    return QRect(topLeft, scaled);
}

QPoint PixelCanvas::pixelAt(const QPoint &position) const
{
    const QRect rect = imageRect();
    if (rect.isEmpty() || !m_document)
        return QPoint(-1, -1);
    const QSize frame = m_document->frameSize();
    const QPoint relative = position - rect.topLeft();
    if (relative.x() < 0 || relative.y() < 0)
        return QPoint(-1, -1);
    const int x = relative.x() / m_zoom;
    const int y = relative.y() / m_zoom;
    if (x < 0 || y < 0 || x >= frame.width() || y >= frame.height())
        return QPoint(-1, -1);
    return QPoint(x, y);
}

bool PixelCanvas::pixelValid(const QPoint &pixel) const
{
    if (!m_document || !m_document->isValid())
        return false;
    const QSize frame = m_document->frameSize();
    return pixel.x() >= 0 && pixel.y() >= 0 && pixel.x() < frame.width()
           && pixel.y() < frame.height();
}

QPoint PixelCanvas::framePointForMode(const QPoint &point, int mode) const
{
    if (!m_document)
        return point;
    const QSize frame = m_document->frameSize();
    int x = point.x();
    int y = point.y();
    if (mode & static_cast<int>(PixelEdit::Symmetry::Horizontal))
        x = frame.width() - 1 - x;
    if (mode & static_cast<int>(PixelEdit::Symmetry::Vertical))
        y = frame.height() - 1 - y;
    return QPoint(x, y);
}

void PixelCanvas::fitToViewport()
{
    if (!m_document || !m_document->isValid()) {
        m_needsFit = false;
        return;
    }
    const QSize frame = m_document->frameSize();
    if (frame.isEmpty())
        return;
    const int availableWidth = qMax(64, width() - 48);
    const int availableHeight = qMax(64, height() - 48);
    m_zoom = qBound(1, qMin(availableWidth / frame.width(), availableHeight / frame.height()),
                    64);
    m_pan = QPointF();
    m_needsFit = false;
    update();
    if (onZoomChanged)
        onZoomChanged(zoomPercent());
}

void PixelCanvas::updateHover(const QPoint &position)
{
    const QPoint pixel = pixelAt(position);
    const bool valid = pixelValid(pixel);
    if (valid == m_hasHover && (!valid || pixel == m_hover))
        return;
    m_hasHover = valid;
    m_hover = valid ? pixel : QPoint(-1, -1);
    if (onHover) {
        const QColor color = valid && m_document
                                 ? PixelEdit::colorAt(m_document->frame(m_frame), pixel.x(),
                                                      pixel.y())
                                 : Qt::transparent;
        onHover(m_hover, color);
    }
    update();
}

void PixelCanvas::pickColor(const QPoint &pixel)
{
    if (!m_document || !pixelValid(pixel))
        return;
    const QColor color =
        PixelEdit::colorAt(m_document->frame(m_frame), pixel.x(), pixel.y());
    m_color = color;
    if (onColorPicked)
        onColorPicked(color);
}

void PixelCanvas::beginDraw(const QPoint &pixel)
{
    if (!m_document || !m_document->isValid() || !pixelValid(pixel))
        return;

    m_drawing = true;
    m_lastPixel = pixel;
    const QColor color =
        m_tool == Tool::Eraser ? QColor(Qt::transparent) : m_color;
    QImage &frame = m_document->frameRef(m_frame);

    switch (m_tool) {
    case Tool::Pen:
    case Tool::Eraser:
        drawSymmetricPoint(frame, pixel, color);
        break;
    case Tool::Bucket: {
        const int symmetry = static_cast<int>(m_symmetry);
        for (int mode = 0; mode <= 3; ++mode) {
            if ((mode & ~symmetry) != 0)
                continue;
            PixelEdit::floodFill(frame, framePointForMode(pixel, mode), color,
                                 m_bucketContiguous, m_bucketTolerance);
        }
        break;
    }
    case Tool::Line:
    case Tool::Rect:
    case Tool::Ellipse:
        m_shapeActive = true;
        m_shapeStart = pixel;
        m_shapeEnd = pixel;
        drawShapePreview();
        break;
    default:
        break;
    }
    update();
}

void PixelCanvas::continueDraw(const QPoint &pixel)
{
    if (!m_drawing || !pixelValid(pixel))
        return;

    const QColor color =
        m_tool == Tool::Eraser ? QColor(Qt::transparent) : m_color;
    QImage &frame = m_document->frameRef(m_frame);

    if (m_tool == Tool::Pen || m_tool == Tool::Eraser) {
        if (pixel != m_lastPixel) {
            drawSymmetricLine(frame, m_lastPixel, pixel, color);
            m_lastPixel = pixel;
            update();
        }
    } else if (m_tool == Tool::Line || m_tool == Tool::Rect
               || m_tool == Tool::Ellipse) {
        m_shapeEnd = pixel;
        drawShapePreview();
        update();
    }
}

void PixelCanvas::finishDraw()
{
    if (!m_drawing)
        return;

    if (m_shapeActive && !m_previewImage.isNull() && m_document)
        m_document->frameRef(m_frame) = m_previewImage;

    m_drawing = false;
    m_shapeActive = false;
    m_previewImage = QImage();

    if (m_document)
        m_document->commitEdit();
    if (onEdited)
        onEdited();
    update();
}

void PixelCanvas::drawSymmetricPoint(QImage &image, const QPoint &point, const QColor &color)
{
    if (!m_document)
        return;
    const int symmetry = static_cast<int>(m_symmetry);
    for (int mode = 0; mode <= 3; ++mode) {
        if ((mode & ~symmetry) != 0)
            continue;
        PixelEdit::drawPoint(image, framePointForMode(point, mode), color, m_brushSize);
    }
}

void PixelCanvas::drawSymmetricLine(QImage &image, const QPoint &a, const QPoint &b,
                                    const QColor &color)
{
    if (!m_document)
        return;
    const int symmetry = static_cast<int>(m_symmetry);
    for (int mode = 0; mode <= 3; ++mode) {
        if ((mode & ~symmetry) != 0)
            continue;
        PixelEdit::drawLine(image, framePointForMode(a, mode), framePointForMode(b, mode),
                            color, m_brushSize);
    }
}

void PixelCanvas::drawShapePreview()
{
    if (!m_document || !m_document->isValid())
        return;

    m_previewImage = m_document->frame(m_frame);
    const int symmetry = static_cast<int>(m_symmetry);
    const QRect raw = QRect(m_shapeStart, m_shapeEnd).normalized();

    for (int mode = 0; mode <= 3; ++mode) {
        if ((mode & ~symmetry) != 0)
            continue;
        if (m_tool == Tool::Line) {
            PixelEdit::drawLine(m_previewImage, framePointForMode(m_shapeStart, mode),
                                framePointForMode(m_shapeEnd, mode), m_color, m_brushSize);
        } else {
            const QRect rect = PixelEdit::mirrorRect(raw, m_document->frameSize(),
                                                     static_cast<PixelEdit::Symmetry>(mode));
            if (m_tool == Tool::Rect) {
                PixelEdit::drawRect(m_previewImage, rect, m_color, m_brushSize,
                                    m_shapeFill);
            } else if (m_tool == Tool::Ellipse) {
                PixelEdit::drawEllipse(m_previewImage, rect, m_color, m_brushSize,
                                       m_shapeFill);
            }
        }
    }
}

void PixelCanvas::applyMovingSelection()
{
    if (!m_document || !m_document->isValid() || m_floatImage.isNull())
        return;

    QImage result = m_floatBase;
    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(m_floatSelection.topLeft() + m_moveOffset, m_floatImage);
    painter.end();

    m_document->frameRef(m_frame) = result;
    m_selection = m_floatSelection.translated(m_moveOffset)
                      .intersected(QRect(QPoint(0, 0), m_document->frameSize()));
    m_document->commitEdit();

    m_movingSelection = false;
    m_floatImage = QImage();
    m_floatBase = QImage();
    if (onSelectionChanged)
        onSelectionChanged();
    if (onEdited)
        onEdited();
    update();
}

void PixelCanvas::drawCheckerboard(QPainter &painter, const QRect &rect) const
{
    const Theme::Colors colors = Theme::colors();
    painter.fillRect(rect, colors.checkerA);
    constexpr int cell = 8;
    painter.save();
    painter.setClipRect(rect);
    for (int y = rect.top(); y < rect.bottom(); y += cell) {
        for (int x = rect.left(); x < rect.right(); x += cell) {
            if ((((x - rect.left()) / cell) + ((y - rect.top()) / cell)) % 2 == 0)
                continue;
            painter.fillRect(QRect(x, y, cell, cell), colors.checkerB);
        }
    }
    painter.restore();
}

QRect PixelCanvas::tiledBounds() const
{
    const QRect bounds = imageRect();
    if (m_tileMode == TileMode::None || bounds.isEmpty())
        return bounds;

    const int tw = bounds.width();
    const int th = bounds.height();
    if (tw <= 0 || th <= 0)
        return bounds;

    const bool horizontal = m_tileMode == TileMode::Both || m_tileMode == TileMode::Horizontal;
    const bool vertical = m_tileMode == TileMode::Both || m_tileMode == TileMode::Vertical;

    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;
    if (horizontal) {
        left = qMax(0, (bounds.left() + tw - 1) / tw);
        right = qMax(0, (width() - 1 - bounds.right() + tw - 1) / tw);
    }
    if (vertical) {
        top = qMax(0, (bounds.top() + th - 1) / th);
        bottom = qMax(0, (height() - 1 - bounds.bottom() + th - 1) / th);
    }

    return QRect(bounds.left() - left * tw, bounds.top() - top * th,
                 bounds.width() + (left + right) * tw,
                 bounds.height() + (top + bottom) * th);
}

void PixelCanvas::drawTiledCopies(QPainter &painter, const QRect &bounds,
                                  const QImage &image) const
{
    if (image.isNull() || bounds.isEmpty() || m_zoom <= 0)
        return;
    const QRect area = tiledBounds();
    if (area.isEmpty())
        return;

    // Fill with a texture brush under a scale transform; this repeats the
    // texture without building a huge scaled pixmap at high zoom levels.
    QRectF fill;
    const qreal zoom = m_zoom;
    if (m_tileMode == TileMode::Both) {
        fill = QRectF((area.left() - bounds.left()) / zoom,
                      (area.top() - bounds.top()) / zoom, area.width() / zoom,
                      area.height() / zoom);
    } else if (m_tileMode == TileMode::Horizontal) {
        fill = QRectF((area.left() - bounds.left()) / zoom, 0, area.width() / zoom,
                      image.height());
    } else {
        fill = QRectF(0, (area.top() - bounds.top()) / zoom, image.width(),
                      area.height() / zoom);
    }

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.translate(bounds.topLeft());
    painter.scale(zoom, zoom);
    painter.setBrushOrigin(QPoint(0, 0));
    painter.fillRect(fill, QBrush(image));
    painter.restore();
}

void PixelCanvas::paintEvent(QPaintEvent *)
{
    const Theme::Colors colors = Theme::colors();
    QPainter painter(this);
    painter.fillRect(rect(), colors.canvas);

    if (!m_document || !m_document->isValid()) {
        painter.setPen(colors.textMuted);
        painter.drawText(rect(), Qt::AlignCenter, tr("无法加载纹理"));
        return;
    }

    const QRect imageBounds = imageRect();
    if (imageBounds.isEmpty())
        return;

    const bool tiling = m_tileMode != TileMode::None;
    drawCheckerboard(painter, tiling ? tiledBounds() : imageBounds);

    if (!m_referenceAbove)
        drawReference(painter);

    if (m_onionSkin && m_document->animated() && m_document->frameCount() > 1) {
        const int count = m_document->frameCount();
        const int previous = (m_frame - 1 + count) % count;
        const int next = (m_frame + 1) % count;
        painter.setOpacity(0.22);
        painter.drawImage(imageBounds, m_document->frame(previous));
        painter.setOpacity(0.14);
        painter.drawImage(imageBounds, m_document->frame(next));
        painter.setOpacity(1.0);
    }

    QImage image;
    if (m_shapeActive && !m_previewImage.isNull())
        image = m_previewImage;
    else if (m_movingSelection && !m_floatBase.isNull())
        image = m_floatBase;
    else
        image = m_document->frame(m_frame);

    if (!image.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        if (tiling)
            drawTiledCopies(painter, imageBounds, image);
        painter.drawImage(imageBounds, image);
    }

    if (m_referenceAbove)
        drawReference(painter);

    paintMaskOverlay(painter);
    paintLassoPreview(painter);

    if (m_movingSelection && !m_floatImage.isNull()) {
        const QRect moved = m_floatSelection.translated(m_moveOffset);
        const QRect target(imageBounds.topLeft()
                               + QPoint(moved.x() * m_zoom, moved.y() * m_zoom),
                           QSize(moved.width() * m_zoom, moved.height() * m_zoom));
        painter.drawImage(target, m_floatImage);
        QPen pen(colors.accent);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(target.adjusted(0, 0, -1, -1));
    } else if (!m_selection.isEmpty()) {
        const QRect target(imageBounds.topLeft()
                               + QPoint(m_selection.x() * m_zoom, m_selection.y() * m_zoom),
                           QSize(m_selection.width() * m_zoom,
                                 m_selection.height() * m_zoom));
        QPen pen(colors.accent);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(target.adjusted(0, 0, -1, -1));
    }

    if (m_showGrid && m_zoom >= 6) {
        painter.setPen(QPen(QColor(colors.text.red(), colors.text.green(),
                                    colors.text.blue(), 26),
                            1));
        const QSize frame = m_document->frameSize();
        for (int x = 0; x <= frame.width(); ++x) {
            const int sx = imageBounds.left() + x * m_zoom;
            painter.drawLine(sx, imageBounds.top(), sx, imageBounds.bottom());
        }
        for (int y = 0; y <= frame.height(); ++y) {
            const int sy = imageBounds.top() + y * m_zoom;
            painter.drawLine(imageBounds.left(), sy, imageBounds.right(), sy);
        }
    }

    if (m_symmetry != PixelEdit::Symmetry::None) {
        QPen pen(QColor(colors.accent.red(), colors.accent.green(), colors.accent.blue(),
                        150));
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        const int mode = static_cast<int>(m_symmetry);
        if (mode & static_cast<int>(PixelEdit::Symmetry::Horizontal)) {
            const int sx = imageBounds.left() + imageBounds.width() / 2;
            painter.drawLine(sx, imageBounds.top(), sx, imageBounds.bottom());
        }
        if (mode & static_cast<int>(PixelEdit::Symmetry::Vertical)) {
            const int sy = imageBounds.top() + imageBounds.height() / 2;
            painter.drawLine(imageBounds.left(), sy, imageBounds.right(), sy);
        }
    }

    if (m_hasHover) {
        const QRect target(imageBounds.topLeft()
                               + QPoint(m_hover.x() * m_zoom, m_hover.y() * m_zoom),
                           QSize(m_zoom, m_zoom));
        painter.setPen(QPen(QColor(colors.text.red(), colors.text.green(),
                                   colors.text.blue(), 180),
                            1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(target.adjusted(0, 0, -1, -1));
    }

    painter.setPen(QPen(tiling ? colors.accent : colors.borderStrong, tiling ? 2 : 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(imageBounds.adjusted(0, 0, -1, -1));
}

void PixelCanvas::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);

    const QPoint position = event->position().toPoint();
    if (event->button() == Qt::LeftButton && hasReferenceImage()
        && m_referenceVisible) {
        if (m_referencePick || (event->modifiers() & Qt::AltModifier)) {
            QColor color;
            if (referenceColorAt(position, &color)) {
                if (onReferenceColorPicked)
                    onReferenceColorPicked(color);
                event->accept();
                return;
            }
        }
        if (m_referenceAdjust && !m_referenceLocked) {
            m_referenceDragging = true;
            m_referenceDragStart = position;
            m_referenceOffsetStart = m_referenceOffset;
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::RightButton) {
        const QPoint pixel = pixelAt(event->position().toPoint());
        if (pixelValid(pixel))
            pickColor(pixel);
        event->accept();
        return;
    }

    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && m_spacePressed)) {
        m_panning = true;
        m_lastPan = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }

    if (event->button() != Qt::LeftButton)
        return;

    const QPoint pixel = pixelAt(event->position().toPoint());
    if (!pixelValid(pixel))
        return;

    if (m_tool == Tool::Lasso) {
        beginTintMask();
        m_lassoActive = true;
        m_lassoPoints.clear();
        m_lassoPoints.append(pixel);
        update();
        event->accept();
        return;
    }

    if (m_tool == Tool::MagicWand) {
        applyWandMask(pixel);
        event->accept();
        return;
    }

    if (m_tool == Tool::Select) {
        if (!m_selection.isEmpty() && m_selection.contains(pixel)) {
            m_movingSelection = true;
            m_moveStart = pixel;
            m_moveOffset = QPoint(0, 0);
            m_floatSelection = m_selection;
            m_floatImage = m_document->frame(m_frame).copy(m_selection);
            m_floatBase = m_document->frame(m_frame);
            QPainter painter(&m_floatBase);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            painter.fillRect(m_selection, Qt::transparent);
            painter.end();
        } else {
            m_selecting = true;
            m_selectStart = pixel;
            m_selectEnd = pixel;
            m_selection = QRect(pixel, pixel);
            if (onSelectionChanged)
                onSelectionChanged();
        }
        update();
        event->accept();
        return;
    }

    if (m_tool == Tool::Eyedropper) {
        pickColor(pixel);
        event->accept();
        return;
    }

    beginDraw(pixel);
    event->accept();
}

void PixelCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_referenceDragging) {
        m_referenceOffset =
            m_referenceOffsetStart
            + QPointF(event->position().toPoint() - m_referenceDragStart);
        update();
        if (onReferenceChanged)
            onReferenceChanged();
        event->accept();
        return;
    }

    if (m_panning) {
        const QPoint delta = event->position().toPoint() - m_lastPan;
        m_lastPan = event->position().toPoint();
        m_pan += QPointF(delta.x(), delta.y());
        update();
        event->accept();
        return;
    }

    updateHover(event->position().toPoint());

    if (!m_document)
        return;

    const QPoint pixel = pixelAt(event->position().toPoint());
    if (m_lassoActive) {
        if (pixelValid(pixel)
            && (m_lassoPoints.isEmpty() || m_lassoPoints.last() != pixel)) {
            m_lassoPoints.append(pixel);
            update();
        }
        return;
    }
    if (m_selecting) {
        m_selectEnd = pixelValid(pixel) ? pixel : m_selectEnd;
        m_selection = QRect(m_selectStart, m_selectEnd).normalized()
                          .intersected(QRect(QPoint(0, 0), m_document->frameSize()));
        update();
    } else if (m_movingSelection) {
        if (pixelValid(pixel)) {
            m_moveOffset = pixel - m_moveStart;
            update();
        }
    } else if (m_drawing) {
        continueDraw(pixel);
    }
}

void PixelCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_referenceDragging && event->button() == Qt::LeftButton) {
        m_referenceDragging = false;
        setCursor(m_referenceAdjust ? Qt::OpenHandCursor : Qt::CrossCursor);
        update();
        event->accept();
        return;
    }

    if (m_panning) {
        m_panning = false;
        setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }

    if (m_lassoActive && event->button() == Qt::LeftButton) {
        finishLassoMask();
        event->accept();
        return;
    }

    if (m_selecting) {
        m_selecting = false;
        if (m_selection.width() <= 0 || m_selection.height() <= 0)
            m_selection = QRect();
        if (onSelectionChanged)
            onSelectionChanged();
        update();
        event->accept();
        return;
    }

    if (m_movingSelection) {
        applyMovingSelection();
        event->accept();
        return;
    }

    if (m_drawing) {
        finishDraw();
        event->accept();
        return;
    }

    QWidget::mouseReleaseEvent(event);
}

void PixelCanvas::wheelEvent(QWheelEvent *event)
{
    if (m_referenceAdjust && !m_referenceLocked && hasReferenceImage()
        && m_referenceVisible) {
        const int steps = event->angleDelta().y() > 0 ? 1 : -1;
        if (event->modifiers() & Qt::ShiftModifier) {
            m_referenceRotation += steps * 5.0;
            while (m_referenceRotation >= 360.0)
                m_referenceRotation -= 360.0;
            while (m_referenceRotation < 0.0)
                m_referenceRotation += 360.0;
        } else {
            const qreal factor = steps > 0 ? 1.1 : 1.0 / 1.1;
            m_referenceScale = qBound(0.05, m_referenceScale * factor, 16.0);
        }
        update();
        if (onReferenceChanged)
            onReferenceChanged();
        event->accept();
        return;
    }

    if (event->modifiers() & Qt::ControlModifier) {
        const int oldZoom = m_zoom;
        const int steps = event->angleDelta().y() > 0 ? 1 : -1;
        const int next = qBound(1, oldZoom + steps, 64);
        if (next == oldZoom) {
            event->accept();
            return;
        }

        const QPoint anchor = event->position().toPoint();
        const QPointF before =
            QPointF(anchor - imageRect().topLeft()) / qreal(oldZoom);
        m_zoom = next;
        const QPointF desired = anchor - before * qreal(next);
        m_pan += desired - imageRect().topLeft();
        m_needsFit = false;
        update();
        if (onZoomChanged)
            onZoomChanged(zoomPercent());
        event->accept();
        return;
    }

    QPointF delta = event->pixelDelta();
    if (delta.isNull())
        delta = QPointF(event->angleDelta().x(), event->angleDelta().y()) / 120.0 * 60.0;
    if ((event->modifiers() & Qt::ShiftModifier) && qFuzzyIsNull(delta.x()))
        delta.setX(delta.y());
    m_pan -= delta;
    update();
    event->accept();
}

void PixelCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = true;
        setCursor(Qt::OpenHandCursor);
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        if (m_lassoActive) {
            m_lassoActive = false;
            m_lassoPoints.clear();
            update();
        } else if (m_shapeActive) {
            m_shapeActive = false;
            m_previewImage = QImage();
            update();
        } else {
            clearSelection();
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) {
        zoomIn();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Minus) {
        zoomOut();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PixelCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = false;
        if (!m_panning)
            setCursor(Qt::CrossCursor);
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void PixelCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_needsFit)
        fitToViewport();
}

void PixelCanvas::leaveEvent(QEvent *event)
{
    if (m_hasHover) {
        m_hasHover = false;
        m_hover = QPoint(-1, -1);
        update();
    }
    QWidget::leaveEvent(event);
}

void PixelCanvas::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        if (!m_tintMask.isNull())
            rebuildMaskOverlay();
        update();
    }
}

} // namespace mcstudio
