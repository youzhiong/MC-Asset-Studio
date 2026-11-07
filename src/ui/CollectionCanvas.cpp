#include "ui/CollectionCanvas.h"

#include "ui/Theme.h"

#include <QDateTime>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>
#include <cmath>
#include <limits>

namespace mcstudio {
namespace {

constexpr int kMargin = 20;
constexpr int kGroupGap = 20;
constexpr int kImageGap = 4;
constexpr int kFramePadding = 6;
constexpr qreal kTextureInset = 1.0; // room for the status frame outside the pixels

} // namespace

class CollectionTextureItem : public QGraphicsItem
{
public:
    explicit CollectionTextureItem(TextureBoardEntry entry)
        : m_entry(std::move(entry))
    {
        setToolTip(m_entry.path
                   + (m_entry.referenceOnly
                          ? QStringLiteral("\n参考材质（未导入）")
                          : QStringLiteral("\n资源包材质")));
        setFlag(QGraphicsItem::ItemIsSelectable, true);
    }

    QString path() const { return m_entry.path; }

    QRectF boundingRect() const override
    {
        const QImage &image = currentFrame();
        if (image.isNull())
            return QRectF(0, 0, 1, 1);
        return QRectF(0, 0, image.width() + 2 * kTextureInset,
                      image.height() + 2 * kTextureInset);
    }

    void advanceAnimation(int elapsedMs)
    {
        if (!m_entry.animated || m_entry.frames.size() < 2)
            return;

        m_elapsedMs += elapsedMs;
        bool changed = false;
        int guard = 0;
        while (m_elapsedMs >= frameDuration() && guard++ < 1000) {
            m_elapsedMs -= frameDuration();
            m_frame = (m_frame + 1) % m_entry.frames.size();
            changed = true;
        }
        if (changed) {
            prepareGeometryChange();
            update();
        }
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *,
               QWidget *) override
    {
        const Theme::Colors colors = Theme::colors();
        const QImage frame = currentFrame();
        if (frame.isNull())
            return;
        painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter->drawImage(QPointF(kTextureInset, kTextureInset), frame);

        // Draw the state frame in the padding outside the pixels so it never
        // covers texture content. Thin enough to stay inside the 4-unit gap.
        QColor borderColor = m_entry.referenceOnly ? colors.reference : colors.pack;
        if (isSelected())
            borderColor = colors.accent;
        painter->setPen(QPen(borderColor, 0.35));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(QRectF(kTextureInset - 0.4, kTextureInset - 0.4,
                                 frame.width() + 0.8, frame.height() + 0.8));
    }

private:
    QImage currentFrame() const
    {
        if (m_entry.frames.isEmpty())
            return m_entry.image;
        return m_entry.frames.value(m_frame);
    }

    int frameDuration() const
    {
        int ticks = m_entry.animation.frametime;
        if (m_frame < m_entry.animation.frames.size())
            ticks = m_entry.animation.frames.at(m_frame).time;
        return qMax(1, ticks) * 50;
    }

    TextureBoardEntry m_entry;
    int m_frame = 0;
    qint64 m_elapsedMs = 0;
};

CollectionCanvas::CollectionCanvas(QWidget *parent)
    : QGraphicsView(parent)
{
    setObjectName(QStringLiteral("CollectionCanvas"));
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setFrameShape(QFrame::NoFrame);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setBackgroundBrush(Theme::colors().canvas);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setFocusPolicy(Qt::StrongFocus);

    m_timer = new QTimer(this);
    m_timer->setInterval(30);
    connect(m_timer, &QTimer::timeout, this, &CollectionCanvas::tickAnimation);
    m_timer->start();

    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        for (QGraphicsItem *item : m_scene->selectedItems()) {
            if (auto *texture = dynamic_cast<CollectionTextureItem *>(item)) {
                if (onTextureSelected)
                    onTextureSelected(texture->path());
                return;
            }
        }
        if (onTextureSelected)
            onTextureSelected(QString());
    });
}

void CollectionCanvas::setTargetResolution(int resolution)
{
    m_targetResolution = qBound(8, resolution, 512);
}

void CollectionCanvas::setCanvasBlocks(int blocks)
{
    m_canvasBlocks = qBound(4, blocks, 512);
}

void CollectionCanvas::setGroups(const QVector<CollectionGroupData> &groups)
{
    m_scene->clear();
    m_items.clear();

    const int contentCanvasWidth = m_canvasBlocks * 16;

    QFont titleFont;
    titleFont.setPixelSize(7);
    titleFont.setBold(true);
    QFont subtitleFont;
    subtitleFont.setPixelSize(5);
    QFont labelFont;
    labelFont.setPixelSize(4);
    const QFontMetrics labelMetrics(labelFont);
    const int labelGap = 2;
    const int labelHeight = labelFont.pixelSize() + 2;
    const QColor labelColor = Theme::colors().pack;

    struct Cell {
        const TextureBoardEntry *entry = nullptr;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        int imageHeight = 0;
        QString label;
    };

    const auto makeCell = [&](const TextureBoardEntry &entry) {
        Cell cell;
        cell.entry = &entry;
        const QImage &image = entry.frames.isEmpty() ? entry.image : entry.frames.first();
        // Collection mode is a 1:1 pixel canvas: a 16x texture occupies 16
        // units, a 32x texture occupies 32 units. No resolution normalization.
        const int cellWidth = qMax(1, image.width() + int(2 * kTextureInset));
        const QString fileName = QFileInfo(entry.path).completeBaseName();
        cell.label = labelMetrics.elidedText(fileName, Qt::ElideMiddle,
                                             qMax(8, cellWidth - 1));
        cell.width = cellWidth;
        cell.imageHeight = qMax(1, image.height() + int(2 * kTextureInset));
        cell.height = cell.imageHeight + labelGap + labelHeight;
        return cell;
    };

    int cursorX = kMargin;
    int cursorY = kMargin;
    int shelfHeight = 0;

    for (const CollectionGroupData &group : groups) {
        if (group.entries.isEmpty())
            continue;

        if (!group.framed && shelfHeight > 0) {
            cursorY += shelfHeight + kGroupGap;
            cursorX = kMargin;
            shelfHeight = 0;
        }

        const int framePadding = group.framed ? kFramePadding : 0;
        const int headerHeight = group.subtitle.isEmpty() ? 16 : 27;
        QVector<Cell> cells;
        for (const TextureBoardEntry &entry : group.entries) {
            const QImage &image = entry.frames.isEmpty() ? entry.image : entry.frames.first();
            if (!image.isNull())
                cells.append(makeCell(entry));
        }
        if (cells.isEmpty())
            continue;

        int contentWidth = 0;
        int contentHeight = 0;

        if (group.framed) {
            // Compact 2-column grid, matching the block/item grouping sketch.
            int rowY = 0;
            for (int index = 0; index < cells.size(); index += 2) {
                Cell &left = cells[index];
                const bool hasRight = index + 1 < cells.size();
                left.x = 0;
                left.y = rowY;
                int rowWidth = left.width;
                int rowHeight = left.height;
                if (hasRight) {
                    Cell &right = cells[index + 1];
                    right.x = left.width + kImageGap;
                    right.y = rowY;
                    rowWidth += kImageGap + right.width;
                    rowHeight = qMax(rowHeight, right.height);
                }
                contentWidth = qMax(contentWidth, rowWidth);
                rowY += rowHeight + kImageGap;
            }
            contentHeight = qMax(0, rowY - kImageGap);
        } else {
            // Skyline packing: place the next image at the lowest available
            // position across the fixed canvas width. This lets small textures
            // fill the empty area beside a large one instead of forcing a row
            // as tall as the largest image.
            const int maxWidth = contentCanvasWidth;
            std::sort(cells.begin(), cells.end(),
                      [](const Cell &a, const Cell &b) {
                          const int areaA = a.width * a.height;
                          const int areaB = b.width * b.height;
                          if (areaA != areaB)
                              return areaA > areaB;
                          return a.width > b.width;
                      });

            struct Skyline {
                int x = 0;
                int width = 0;
                int height = 0;
            };
            QVector<Skyline> skyline;
            skyline.append({0, maxWidth, 0});

            for (Cell &cell : cells) {
                const int footprintWidth = cell.width + kImageGap;
                const int footprintHeight = cell.height + kImageGap;
                const int imageWidth = qMin(footprintWidth, maxWidth);
                int bestX = 0;
                int bestY = std::numeric_limits<int>::max();

                QVector<int> candidates;
                candidates.append(0);
                candidates.append(maxWidth - imageWidth);
                for (const Skyline &segment : skyline) {
                    candidates.append(segment.x);
                    candidates.append(segment.x + segment.width - imageWidth);
                }

                for (int candidate : candidates) {
                    if (candidate < 0 || candidate + imageWidth > maxWidth)
                        continue;
                    int y = 0;
                    for (const Skyline &segment : skyline) {
                        if (segment.x + segment.width <= candidate
                            || segment.x >= candidate + imageWidth) {
                            continue;
                        }
                        y = qMax(y, segment.height);
                    }
                    if (y < bestY || (y == bestY && candidate < bestX)) {
                        bestY = y;
                        bestX = candidate;
                    }
                }
                if (bestY == std::numeric_limits<int>::max())
                    bestY = 0;

                cell.x = bestX;
                cell.y = bestY;
                const int top = bestY + footprintHeight;
                const int footprintRight = bestX + imageWidth;

                QVector<Skyline> next;
                for (const Skyline &segment : skyline) {
                    const int left = segment.x;
                    const int right = segment.x + segment.width;
                    if (right <= bestX || left >= footprintRight) {
                        next.append(segment);
                        continue;
                    }
                    if (left < bestX)
                        next.append({left, bestX - left, segment.height});
                    if (right > footprintRight) {
                        next.append({footprintRight, right - footprintRight,
                                     segment.height});
                    }
                }
                next.append({bestX, imageWidth, top});
                std::sort(next.begin(), next.end(),
                          [](const Skyline &a, const Skyline &b) { return a.x < b.x; });
                QVector<Skyline> merged;
                for (const Skyline &segment : next) {
                    if (!merged.isEmpty()
                        && merged.last().height == segment.height
                        && merged.last().x + merged.last().width == segment.x) {
                        merged.last().width += segment.width;
                    } else {
                        merged.append(segment);
                    }
                }
                skyline = merged;
                contentWidth = qMax(contentWidth, cell.x + cell.width);
                contentHeight = qMax(contentHeight, cell.y + cell.height);
            }
            contentWidth = qMax(contentWidth, maxWidth);
        }

        QFontMetrics titleMetrics(titleFont);
        QFontMetrics subtitleMetrics(subtitleFont);
        const int titleWidth = titleMetrics.horizontalAdvance(group.title);
        const int subtitleWidth = group.subtitle.isEmpty()
                                      ? 0
                                      : subtitleMetrics.horizontalAdvance(group.subtitle);
        int groupWidth = qMax(contentWidth, qMax(titleWidth, subtitleWidth))
                         + 2 * framePadding;
        if (!group.framed)
            groupWidth = contentCanvasWidth;
        const int groupHeight = headerHeight + contentHeight + 2 * framePadding;

        if (group.framed && cursorX > kMargin
            && cursorX + groupWidth > kMargin + contentCanvasWidth) {
            cursorX = kMargin;
            cursorY += shelfHeight + kGroupGap;
            shelfHeight = 0;
        }

        const int groupX = group.framed ? cursorX : kMargin;
        const int groupY = cursorY;
        const int contentX = groupX + framePadding;
        const int contentY = groupY + framePadding + headerHeight;

        auto *title = new QGraphicsSimpleTextItem(group.title);
        title->setFont(titleFont);
        title->setBrush(Theme::colors().text);
        title->setPos(contentX, groupY + framePadding - 1);
        m_scene->addItem(title);

        if (!group.subtitle.isEmpty()) {
            auto *subtitle = new QGraphicsSimpleTextItem(group.subtitle);
            subtitle->setFont(subtitleFont);
            subtitle->setBrush(Theme::colors().textFaint);
            subtitle->setPos(contentX, groupY + framePadding + 10);
            m_scene->addItem(subtitle);
        }

        for (const Cell &cell : cells) {
            auto *item = new CollectionTextureItem(*cell.entry);
            item->setScale(1.0);
            item->setPos(contentX + cell.x, contentY + cell.y);
            m_scene->addItem(item);
            m_items.append(item);

            auto *label = new QGraphicsSimpleTextItem(cell.label);
            label->setFont(labelFont);
            label->setBrush(labelColor);
            label->setToolTip(cell.entry->path);
            label->setPos(contentX + cell.x,
                          contentY + cell.y + cell.imageHeight + labelGap);
            m_scene->addItem(label);
        }

        if (group.framed) {
            QColor frameFill = Theme::colors().danger;
            frameFill.setAlpha(12);
            auto *frame = m_scene->addRect(
                QRectF(groupX, groupY, groupWidth, groupHeight),
                QPen(Theme::colors().danger, 1.0),
                QBrush(frameFill));
            frame->setZValue(-1);
            cursorX += groupWidth + kGroupGap;
            shelfHeight = qMax(shelfHeight, groupHeight);
        } else {
            cursorY += groupHeight + kGroupGap;
        }
    }

    const int sceneHeight = qMax(cursorY + shelfHeight + kMargin, 600);
    m_scene->setSceneRect(0, 0, contentCanvasWidth + 2 * kMargin, sceneHeight);
    setZoomPercent(m_zoomPercent);
    m_scene->update();
    viewport()->update();
}

void CollectionCanvas::setZoomPercent(int percent)
{
    m_zoomPercent = qBound(10, percent, 800);
    resetTransform();
    const double scaleValue = m_zoomPercent / 100.0;
    scale(scaleValue, scaleValue);
    if (onZoomChanged)
        onZoomChanged(zoomPercent());
}

int CollectionCanvas::zoomPercent() const
{
    return qRound(transform().m11() * 100.0);
}

void CollectionCanvas::fitAll()
{
    if (m_items.isEmpty()) {
        setZoomPercent(100);
        return;
    }
    fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
    m_zoomPercent = zoomPercent();
    if (onZoomChanged)
        onZoomChanged(m_zoomPercent);
}

void CollectionCanvas::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        double steps = event->angleDelta().y() / 120.0;
        if (qFuzzyIsNull(steps))
            steps = event->pixelDelta().y() / 50.0;
        if (!qFuzzyIsNull(steps))
            zoomBy(std::pow(1.2, steps), event->position().toPoint());
        event->accept();
        return;
    }

    QPointF delta = event->pixelDelta();
    if (delta.isNull())
        delta = QPointF(event->angleDelta().x(), event->angleDelta().y()) / 120.0 * 60.0;
    if ((event->modifiers() & Qt::ShiftModifier) && qFuzzyIsNull(delta.x()))
        delta.setX(delta.y());
    scrollBy(delta);
    event->accept();
}

void CollectionCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPan = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void CollectionCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QPoint delta = event->position().toPoint() - m_lastPan;
        m_lastPan = event->position().toPoint();
        scrollBy(QPointF(-delta.x(), -delta.y()));
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void CollectionCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

bool CollectionCanvas::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture) {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        switch (gesture->gestureType()) {
        case Qt::ZoomNativeGesture:
            zoomBy(1.0 + gesture->value(), gesture->position().toPoint());
            return true;
        case Qt::PanNativeGesture:
            scrollBy(gesture->delta());
            return true;
        case Qt::BeginNativeGesture:
        case Qt::EndNativeGesture:
            return true;
        default:
            break;
        }
    }
    return QGraphicsView::viewportEvent(event);
}

void CollectionCanvas::changeEvent(QEvent *event)
{
    QGraphicsView::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        setBackgroundBrush(Theme::colors().canvas);
        if (viewport())
            viewport()->update();
    }
}

void CollectionCanvas::zoomBy(double factor, const QPoint &anchor)
{
    const double current = transform().m11();
    const double target = qBound(0.10, current * factor, 8.0);
    factor = target / current;
    if (qFuzzyCompare(factor, 1.0))
        return;

    const QPointF before = mapToScene(anchor);
    scale(factor, factor);
    const QPointF after = mapToScene(anchor);
    const QPointF delta = after - before;
    translate(delta.x(), delta.y());
    m_zoomPercent = zoomPercent();
    if (onZoomChanged)
        onZoomChanged(m_zoomPercent);
}

void CollectionCanvas::scrollBy(const QPointF &delta)
{
    if (!qFuzzyIsNull(delta.x()))
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - qRound(delta.x()));
    if (!qFuzzyIsNull(delta.y()))
        verticalScrollBar()->setValue(verticalScrollBar()->value() - qRound(delta.y()));
}

void CollectionCanvas::tickAnimation()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastTick == 0) {
        m_lastTick = now;
        return;
    }
    const int elapsed = int(qBound<qint64>(0LL, now - m_lastTick, 100LL));
    m_lastTick = now;
    for (CollectionTextureItem *item : m_items)
        item->advanceAnimation(elapsed);
}

} // namespace mcstudio
