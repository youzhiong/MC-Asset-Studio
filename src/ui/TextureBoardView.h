#pragma once

#include "core/AnimationMeta.h"

#include <QGraphicsView>
#include <QHash>
#include <QImage>
#include <QPointF>
#include <QVector>
#include <functional>

class QGraphicsScene;
class QResizeEvent;
class QTimer;

namespace mcstudio {

struct TextureBoardEntry {
    QString path;
    QString namespaceId;
    QString modId;
    QString modName;
    QString categoryKey;
    QImage image;
    QVector<QImage> frames;
    AnimationMeta animation;
    bool animated = false;
    QString source;
    bool referenceOnly = false;
    QString referenceLabel;
    // Collection mode: localized block/item name and namespace-qualified ID.
    QString displayName;
    QString displayId;
};

class TextureTileItem;

class TextureBoardView : public QGraphicsView
{
public:
    explicit TextureBoardView(QWidget *parent = nullptr);

    void setEntries(const QVector<TextureBoardEntry> &entries, int tileSize,
                    bool labelsVisible, const QHash<QString, QPointF> &positions);
    void setTileSize(int tileSize);
    void setLabelsVisible(bool visible);
    void setCategoryFilter(const QString &category);
    void setNamespaceFilter(const QString &namespaceId);
    void setSearchFilter(const QString &text);
    void setSourceFilter(const QString &source);
    QString categoryFilter() const { return m_categoryFilter; }
    QString namespaceFilter() const { return m_namespaceFilter; }
    QString searchFilter() const { return m_searchFilter; }
    QString sourceFilter() const { return m_sourceFilter; }
    void setFreeLayout(bool enabled);
    void setAnimationEnabled(bool enabled);
    bool animationEnabled() const { return m_animationEnabled; }
    void resetLayout();
    void fitBoard();
    void setZoomPercent(int percent);
    int zoomPercent() const;
    int tileSize() const { return m_tileSize; }
    bool labelsVisible() const { return m_labelsVisible; }
    bool freeLayout() const { return m_freeLayout; }
    bool hasEntries() const { return !m_items.isEmpty(); }
    int visibleCount() const;
    QHash<QString, QPointF> positions() const { return m_positions; }

    void openTexture(const QString &path);
    void textureSelected(const QString &path);
    void tileMoved(const QString &path, const QPointF &position);
    void replaceEntry(const TextureBoardEntry &entry);

    std::function<void(const QString &)> onOpenTexture;
    std::function<void(const QString &)> onViewTexture;
    std::function<void(const QString &)> onTextureSelected;
    std::function<void(const QString &)> onImportTexture;
    std::function<void(const QString &)> onImportNamespace;
    std::function<void()> onLayoutChanged;
    std::function<void(int)> onZoomChanged;

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void layoutItems();
    void updateSceneRect();
    void applyFilters();
    void zoomBy(double factor, const QPoint &anchor);
    void scrollBy(const QPointF &delta);
    void startAnimation();
    void tickAnimation();

    QGraphicsScene *m_scene = nullptr;
    QVector<TextureTileItem *> m_items;
    QHash<QString, QPointF> m_positions;
    int m_tileSize = 140;
    bool m_labelsVisible = true;
    QString m_categoryFilter = QStringLiteral("all");
    QString m_namespaceFilter;
    QString m_searchFilter;
    QString m_sourceFilter = QStringLiteral("all");
    bool m_freeLayout = false;
    bool m_animationEnabled = true;
    bool m_layouting = false;
    bool m_panning = false;
    QPoint m_lastPan;
    QTimer *m_timer = nullptr;
    qint64 m_lastTick = 0;
};

} // namespace mcstudio
