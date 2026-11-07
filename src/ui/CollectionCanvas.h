#pragma once

#include "ui/TextureBoardView.h"

#include <QGraphicsView>
#include <QVector>
#include <functional>

class QGraphicsScene;
class QTimer;

namespace mcstudio {

struct CollectionGroupData {
    QString title;
    QString subtitle;
    bool framed = false;
    QVector<TextureBoardEntry> entries;
};

class CollectionTextureItem;

class CollectionCanvas : public QGraphicsView
{
public:
    explicit CollectionCanvas(QWidget *parent = nullptr);

    void setGroups(const QVector<CollectionGroupData> &groups);
    void setTargetResolution(int resolution);
    int targetResolution() const { return m_targetResolution; }
    void setCanvasBlocks(int blocks);
    int canvasBlocks() const { return m_canvasBlocks; }
    void setZoomPercent(int percent);
    int zoomPercent() const;
    void fitAll();

    std::function<void(int)> onZoomChanged;
    std::function<void(const QString &)> onTextureSelected;

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void zoomBy(double factor, const QPoint &anchor);
    void scrollBy(const QPointF &delta);
    void tickAnimation();

    QGraphicsScene *m_scene = nullptr;
    QVector<CollectionTextureItem *> m_items;
    QTimer *m_timer = nullptr;
    qint64 m_lastTick = 0;
    bool m_panning = false;
    QPoint m_lastPan;
    int m_zoomPercent = 500;
    int m_targetResolution = 16;
    int m_canvasBlocks = 32;
};

} // namespace mcstudio
