#pragma once

#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QSize>

class QComboBox;
class QLabel;
class QPainter;
class QSlider;

namespace mcstudio {

class CoverCanvas;

class CoverEditorDialog : public QDialog
{
public:
    explicit CoverEditorDialog(const QImage &source, int initialSize = 512,
                               QWidget *parent = nullptr);

    QImage resultImage() const { return m_result; }
    int outputSize() const { return m_outputSize; }

    void paintPreview(QPainter &painter, const QSize &target) const;
    void zoomBy(double factor);
    void panBy(const QPoint &delta);
    void rotateBy(int degrees);

    int canvasSize() const { return m_canvasSize; }
    double zoom() const { return m_zoom; }
    int rotation() const { return m_rotation; }
    QPointF offset() const { return m_offset; }

protected:
    void accept() override;

private:
    QImage renderOutput(int size) const;
    void paintImage(QPainter &painter, const QSize &target) const;
    QSize rotatedSourceSize() const;
    double baseScale(const QSize &target) const;
    void clampOffset();
    void updateZoomLabel();

    QImage m_source;
    QImage m_result;
    CoverCanvas *m_canvas = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QComboBox *m_sizeCombo = nullptr;
    int m_canvasSize = 420;
    int m_outputSize = 512;
    int m_rotation = 0;
    double m_zoom = 1.0;
    QPointF m_offset;
};

} // namespace mcstudio
