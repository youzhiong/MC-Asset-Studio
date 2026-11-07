#include "ui/TextureBoardView.h"

#include "core/TextureCategory.h"
#include "ui/Theme.h"

#include <QDateTime>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QAction>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QObject>
#include <QNativeGestureEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>
#include <cmath>

namespace mcstudio {
namespace {

constexpr int kGridGap = 16;
constexpr int kMargin = 24;

} // namespace

class TextureTileItem : public QGraphicsItem
{
public:
    TextureTileItem(TextureBoardEntry entry, TextureBoardView *board)
        : m_entry(std::move(entry))
        , m_board(board)
    {
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setAcceptHoverEvents(false);
        updateToolTip();
    }

    void setEntry(const TextureBoardEntry &entry)
    {
        m_entry = entry;
        updateToolTip();
        update();
    }

    void updateToolTip()
    {
        setToolTip(m_entry.path
                   + (m_entry.animated
                          ? QStringLiteral("\n动图 · %1 帧").arg(m_entry.frames.size())
                          : QString())
                   + (m_entry.referenceOnly ? QStringLiteral("\n参考材质（未导入）")
                                            : QString()));
    }

    QRectF boundingRect() const override
    {
        return QRectF(0, 0, m_cardSize, cardHeight());
    }

    QString path() const { return m_entry.path; }
    QString category() const
    {
        return m_entry.categoryKey.isEmpty() ? textureCategoryForPath(m_entry.path)
                                             : m_entry.categoryKey;
    }
    QString namespaceId() const { return m_entry.namespaceId; }
    bool isAnimated() const { return m_entry.animated; }
    bool isReference() const { return m_entry.referenceOnly; }

    void setCardSize(int size)
    {
        if (m_cardSize == size)
            return;
        prepareGeometryChange();
        m_cardSize = size;
        update();
    }

    void setLabelsVisible(bool visible)
    {
        if (m_labelsVisible == visible)
            return;
        prepareGeometryChange();
        m_labelsVisible = visible;
        update();
    }

    void setMovable(bool movable)
    {
        setFlag(QGraphicsItem::ItemIsMovable, movable);
        setCursor(movable ? Qt::OpenHandCursor : Qt::ArrowCursor);
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
        if (changed)
            update();
    }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        const Theme::Colors colors = Theme::colors();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRectF card = boundingRect().adjusted(0.5, 0.5, -0.5, -0.5);
        QColor borderColor = m_entry.referenceOnly ? colors.reference : colors.pack;
        qreal borderWidth = 1.8;
        if (isSelected()) {
            borderColor = colors.accent;
            borderWidth = 2.4;
        }
        painter->setPen(QPen(borderColor, borderWidth));
        painter->setBrush(colors.panel);
        painter->drawRoundedRect(card, 8, 8);

        const QRectF imageRect(8, 8, m_cardSize - 16, m_cardSize - 16);
        painter->save();
        painter->setClipRect(imageRect);
        for (int y = 0; y < int(imageRect.height()); y += 8) {
            for (int x = 0; x < int(imageRect.width()); x += 8) {
                const bool dark = ((x / 8) + (y / 8)) % 2 == 0;
                painter->fillRect(QRectF(imageRect.left() + x, imageRect.top() + y,
                                         qMin(8, int(imageRect.width()) - x),
                                         qMin(8, int(imageRect.height()) - y)),
                                  dark ? colors.checkerA : colors.checkerB);
            }
        }
        painter->restore();

        const QImage frame = currentFrame();
        if (!frame.isNull()) {
            const QSizeF target = frame.size().scaled(imageRect.size().toSize(),
                                                      Qt::KeepAspectRatio);
            const QRectF targetRect(imageRect.center().x() - target.width() / 2.0,
                                    imageRect.center().y() - target.height() / 2.0,
                                    target.width(), target.height());
            painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter->drawImage(targetRect, frame);
        } else {
            painter->setPen(colors.textMuted);
            painter->drawText(imageRect, Qt::AlignCenter, QStringLiteral("无图像"));
        }

        if (m_entry.animated)
            drawBadge(painter, QStringLiteral("ANIM"), colors.accentSoft,
                      colors.accentText, false);
        if (m_entry.referenceOnly) {
            drawBadge(painter, QStringLiteral("REF"), colors.badgeBg, colors.warning, true);
        } else {
            drawBadge(painter, QStringLiteral("PACK"), colors.badgeBg, colors.pack, true);
        }

        if (!m_labelsVisible)
            return;

        QFont font = painter->font();
        font.setPointSizeF(8.5);
        painter->setFont(font);
        painter->setPen(colors.text);
        const QFontMetrics metrics(font);
        const QString name = QFileInfo(m_entry.path).fileName();
        painter->drawText(QRectF(8, m_cardSize + 4, m_cardSize - 16, 16),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          metrics.elidedText(name, Qt::ElideMiddle, m_cardSize - 16));

        font.setPointSizeF(7.5);
        painter->setFont(font);
        painter->setPen(colors.textFaint);
        const QFontMetrics smallMetrics(font);
        QString parent = QFileInfo(m_entry.path).path();
        const int texturesMarker = parent.indexOf(QLatin1String("/textures/"));
        if (texturesMarker >= 0)
            parent = parent.mid(texturesMarker + 10);
        if (parent.isEmpty())
            parent = QStringLiteral("textures");
        const QImage image = m_entry.frames.isEmpty() ? m_entry.image : m_entry.frames.first();
        const int resolution = qMax(image.width(), image.height());
        const QString meta = QStringLiteral("%1 · %2×").arg(parent).arg(resolution);
        painter->drawText(QRectF(8, m_cardSize + 22, m_cardSize - 16, 16),
                          Qt::AlignLeft | Qt::AlignVCenter,
                          smallMetrics.elidedText(meta, Qt::ElideMiddle, m_cardSize - 16));
    }

protected:
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (m_board)
            m_board->openTexture(m_entry.path);
        event->accept();
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent *event) override
    {
        if (!m_board) {
            QGraphicsItem::contextMenuEvent(event);
            return;
        }

        QMenu menu;
        QAction *editAction = menu.addAction(QObject::tr("编辑材质"));
        QAction *viewAction = menu.addAction(QObject::tr("查看大图"));
        QAction *importOne = nullptr;
        QAction *importNamespace = nullptr;
        if (m_entry.referenceOnly) {
            menu.addSeparator();
            importOne = menu.addAction(QObject::tr("导入到资源包"));
            importNamespace = menu.addAction(QObject::tr("导入此命名空间全部参考材质"));
        }
        QAction *chosen = menu.exec(event->screenPos());
        if (chosen == editAction) {
            m_board->openTexture(m_entry.path);
        } else if (chosen == viewAction) {
            if (m_board->onViewTexture)
                m_board->onViewTexture(m_entry.path);
        } else if (chosen == importOne && m_board->onImportTexture) {
            m_board->onImportTexture(m_entry.path);
        } else if (chosen == importNamespace && m_board->onImportNamespace) {
            m_board->onImportNamespace(m_entry.namespaceId);
        }
        event->accept();
    }

    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override
    {
        if (change == ItemPositionHasChanged && m_board) {
            m_board->tileMoved(m_entry.path, pos());
        } else if (change == ItemSelectedHasChanged) {
            update();
            if (value.toBool() && m_board)
                m_board->textureSelected(m_entry.path);
        }
        return QGraphicsItem::itemChange(change, value);
    }

private:
    int cardHeight() const { return m_cardSize + (m_labelsVisible ? 44 : 12); }

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

    void drawBadge(QPainter *painter, const QString &text, const QColor &background,
                   const QColor &foreground, bool right) const
    {
        QFont font = painter->font();
        font.setPointSizeF(7.0);
        font.setBold(true);
        painter->setFont(font);
        const QFontMetrics metrics(font);
        const int width = metrics.horizontalAdvance(text) + 10;
        const QRectF rect(right ? m_cardSize - 8 - width : 8, 8, width, 16);
        painter->setPen(Qt::NoPen);
        painter->setBrush(background);
        painter->drawRoundedRect(rect, 4, 4);
        painter->setPen(foreground);
        painter->drawText(rect, Qt::AlignCenter, text);
    }

    TextureBoardEntry m_entry;
    TextureBoardView *m_board = nullptr;
    int m_cardSize = 140;
    bool m_labelsVisible = true;
    int m_frame = 0;
    qint64 m_elapsedMs = 0;
};

TextureBoardView::TextureBoardView(QWidget *parent)
    : QGraphicsView(parent)
{
    setObjectName(QStringLiteral("TextureBoardView"));
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);

    setFrameShape(QFrame::NoFrame);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setDragMode(QGraphicsView::NoDrag);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setBackgroundBrush(Theme::colors().window);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    setFocusPolicy(Qt::StrongFocus);

    m_timer = new QTimer(this);
    m_timer->setInterval(30);
    connect(m_timer, &QTimer::timeout, this, &TextureBoardView::tickAnimation);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        if (m_scene->selectedItems().isEmpty() && onTextureSelected)
            onTextureSelected(QString());
    });
}

void TextureBoardView::setEntries(const QVector<TextureBoardEntry> &entries, int tileSize,
                                  bool labelsVisible,
                                  const QHash<QString, QPointF> &positions)
{
    m_layouting = true;
    m_scene->clear();
    m_items.clear();
    m_positions = positions;
    m_tileSize = qBound(72, tileSize, 260);
    m_labelsVisible = labelsVisible;

    for (const TextureBoardEntry &entry : entries) {
        auto *item = new TextureTileItem(entry, this);
        item->setCardSize(m_tileSize);
        item->setLabelsVisible(m_labelsVisible);
        m_scene->addItem(item);
        m_items.append(item);
    }

    layoutItems();
    setFreeLayout(m_freeLayout);
    m_layouting = false;
    applyFilters();
    updateSceneRect();

    if (m_animationEnabled)
        startAnimation();
}

void TextureBoardView::setCategoryFilter(const QString &category)
{
    m_categoryFilter = category.isEmpty() ? QStringLiteral("all") : category;
    applyFilters();
}

void TextureBoardView::setNamespaceFilter(const QString &namespaceId)
{
    if (m_namespaceFilter == namespaceId)
        return;
    m_namespaceFilter = namespaceId;
    applyFilters();
}

void TextureBoardView::setSearchFilter(const QString &text)
{
    const QString normalized = text.trimmed();
    if (m_searchFilter == normalized)
        return;
    m_searchFilter = normalized;
    applyFilters();
}

void TextureBoardView::setSourceFilter(const QString &source)
{
    const QString normalized = source.isEmpty() ? QStringLiteral("all") : source;
    if (m_sourceFilter == normalized)
        return;
    m_sourceFilter = normalized;
    applyFilters();
}

void TextureBoardView::applyFilters()
{
    m_layouting = true;
    for (TextureTileItem *item : m_items) {
        bool visible = m_namespaceFilter.isEmpty()
                       || item->namespaceId() == m_namespaceFilter;
        if (visible && m_sourceFilter == QLatin1String("reference"))
            visible = item->isReference();
        else if (visible && m_sourceFilter == QLatin1String("pack"))
            visible = !item->isReference();
        if (visible && !m_searchFilter.isEmpty())
            visible = item->path().contains(m_searchFilter, Qt::CaseInsensitive);
        if (visible) {
            if (m_categoryFilter == QLatin1String("all"))
                visible = true;
            else if (m_categoryFilter == QLatin1String("animation"))
                visible = item->isAnimated();
            else
                visible = item->category() == m_categoryFilter;
        }
        item->setVisible(visible);
    }
    layoutItems();
    m_layouting = false;
    updateSceneRect();
}

int TextureBoardView::visibleCount() const
{
    int count = 0;
    for (TextureTileItem *item : m_items) {
        if (item->isVisible())
            ++count;
    }
    return count;
}

void TextureBoardView::setTileSize(int tileSize)
{
    const int bounded = qBound(72, tileSize, 260);
    if (m_tileSize == bounded)
        return;
    m_tileSize = bounded;
    m_layouting = true;
    for (TextureTileItem *item : m_items)
        item->setCardSize(m_tileSize);
    layoutItems();
    m_layouting = false;
}

void TextureBoardView::setLabelsVisible(bool visible)
{
    if (m_labelsVisible == visible)
        return;
    m_labelsVisible = visible;
    m_layouting = true;
    for (TextureTileItem *item : m_items)
        item->setLabelsVisible(visible);
    layoutItems();
    m_layouting = false;
}

void TextureBoardView::setFreeLayout(bool enabled)
{
    m_freeLayout = enabled;
    for (TextureTileItem *item : m_items)
        item->setMovable(enabled);
}

void TextureBoardView::setAnimationEnabled(bool enabled)
{
    m_animationEnabled = enabled;
    if (enabled)
        startAnimation();
    else
        m_timer->stop();
}

void TextureBoardView::resetLayout()
{
    m_positions.clear();
    m_layouting = true;
    layoutItems();
    m_layouting = false;
    if (onLayoutChanged)
        onLayoutChanged();
}

void TextureBoardView::fitBoard()
{
    if (m_items.isEmpty()) {
        resetTransform();
        if (onZoomChanged)
            onZoomChanged(zoomPercent());
        return;
    }
    fitInView(m_scene->itemsBoundingRect().adjusted(-kMargin, -kMargin, kMargin, kMargin),
              Qt::KeepAspectRatio);
    if (onZoomChanged)
        onZoomChanged(zoomPercent());
}

void TextureBoardView::setZoomPercent(int percent)
{
    resetTransform();
    const double scaleValue = qBound(10, percent, 400) / 100.0;
    scale(scaleValue, scaleValue);
    if (onZoomChanged)
        onZoomChanged(zoomPercent());
}

int TextureBoardView::zoomPercent() const
{
    return qRound(transform().m11() * 100.0);
}

void TextureBoardView::openTexture(const QString &path)
{
    if (onOpenTexture)
        onOpenTexture(path);
}

void TextureBoardView::textureSelected(const QString &path)
{
    if (onTextureSelected)
        onTextureSelected(path);
}

void TextureBoardView::tileMoved(const QString &path, const QPointF &position)
{
    if (m_layouting)
        return;
    m_positions.insert(path, position);
    updateSceneRect();
    if (onLayoutChanged)
        onLayoutChanged();
}

void TextureBoardView::replaceEntry(const TextureBoardEntry &entry)
{
    for (TextureTileItem *item : m_items) {
        if (item->path() == entry.path) {
            item->setEntry(entry);
            updateSceneRect();
            return;
        }
    }
}

void TextureBoardView::layoutItems()
{
    int viewWidth = viewport()->width();
    if (viewWidth < 300)
        viewWidth = 900;

    const int columns = qBound(3, (viewWidth - 2 * kMargin + kGridGap)
                                      / qMax(1, m_tileSize + kGridGap),
                               12);
    int gridIndex = 0;
    for (TextureTileItem *item : m_items) {
        if (!item->isVisible())
            continue;
        if (m_positions.contains(item->path())) {
            item->setPos(m_positions.value(item->path()));
            continue;
        }
        const int column = gridIndex % columns;
        const int row = gridIndex / columns;
        ++gridIndex;
        const qreal x = kMargin + column * (m_tileSize + kGridGap);
        const qreal y = kMargin + row * (item->boundingRect().height() + kGridGap);
        item->setPos(x, y);
    }
    updateSceneRect();
}

void TextureBoardView::updateSceneRect()
{
    QRectF bounds;
    for (TextureTileItem *item : m_items) {
        if (item->isVisible())
            bounds = bounds.united(item->sceneBoundingRect());
    }
    if (bounds.isEmpty())
        bounds = QRectF(0, 0, 640, 400);
    m_scene->setSceneRect(bounds.adjusted(-kMargin, -kMargin, kMargin, kMargin));
}

void TextureBoardView::wheelEvent(QWheelEvent *event)
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
    if (delta.isNull()) {
        delta = QPointF(event->angleDelta().x(), event->angleDelta().y())
                / 120.0 * 60.0;
    }
    if ((event->modifiers() & Qt::ShiftModifier) && qFuzzyIsNull(delta.x()))
        delta.setX(delta.y());
    scrollBy(delta);
    event->accept();
}

void TextureBoardView::mousePressEvent(QMouseEvent *event)
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

void TextureBoardView::mouseMoveEvent(QMouseEvent *event)
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

void TextureBoardView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void TextureBoardView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_positions.isEmpty() && !m_items.isEmpty()) {
        m_layouting = true;
        layoutItems();
        m_layouting = false;
    }
}

bool TextureBoardView::viewportEvent(QEvent *event)
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

void TextureBoardView::changeEvent(QEvent *event)
{
    QGraphicsView::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange) {
        setBackgroundBrush(Theme::colors().window);
        if (viewport())
            viewport()->update();
    }
}

void TextureBoardView::zoomBy(double factor, const QPoint &anchor)
{
    const double current = transform().m11();
    const double target = qBound(0.10, current * factor, 4.0);
    factor = target / current;
    if (qFuzzyCompare(factor, 1.0))
        return;

    const QPointF before = mapToScene(anchor);
    scale(factor, factor);
    const QPointF after = mapToScene(anchor);
    const QPointF delta = after - before;
    translate(delta.x(), delta.y());
    if (onZoomChanged)
        onZoomChanged(zoomPercent());
}

void TextureBoardView::scrollBy(const QPointF &delta)
{
    if (!qFuzzyIsNull(delta.x()))
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - qRound(delta.x()));
    if (!qFuzzyIsNull(delta.y()))
        verticalScrollBar()->setValue(verticalScrollBar()->value() - qRound(delta.y()));
}

void TextureBoardView::startAnimation()
{
    m_lastTick = 0;
    if (!m_timer->isActive())
        m_timer->start();
}

void TextureBoardView::tickAnimation()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastTick == 0) {
        m_lastTick = now;
        return;
    }
    const int elapsed = int(qBound<qint64>(0LL, now - m_lastTick, 100LL));
    m_lastTick = now;
    for (TextureTileItem *item : m_items)
        item->advanceAnimation(elapsed);
}

} // namespace mcstudio
