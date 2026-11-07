#include "ui/CoverEditorDialog.h"

#include "ui/Theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QIntValidator>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

namespace mcstudio {

class CoverCanvas : public QWidget
{
public:
    explicit CoverCanvas(CoverEditorDialog *dialog)
        : QWidget(dialog)
        , m_dialog(dialog)
    {
        setFixedSize(dialog->canvasSize(), dialog->canvasSize());
        setCursor(Qt::OpenHandCursor);
        setMouseTracking(true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        m_dialog->paintPreview(painter, size());
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dragging = true;
            m_last = event->pos();
            setCursor(Qt::ClosedHandCursor);
        }
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_dragging)
            return;
        m_dialog->panBy(event->pos() - m_last);
        m_last = event->pos();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            m_dragging = false;
            setCursor(Qt::OpenHandCursor);
        }
    }

    void wheelEvent(QWheelEvent *event) override
    {
        const double factor = std::pow(1.0015, event->angleDelta().y());
        m_dialog->zoomBy(factor);
        update();
    }

private:
    CoverEditorDialog *m_dialog = nullptr;
    bool m_dragging = false;
    QPoint m_last;
};

CoverEditorDialog::CoverEditorDialog(const QImage &source, int initialSize,
                                     QWidget *parent)
    : QDialog(parent)
    , m_source(source)
    , m_outputSize(qBound(1, initialSize, 4096))
{
    setWindowTitle(tr("编辑资源包封面"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(720, 620);
    setMinimumSize(640, 560);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 10);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("编辑封面"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    layout->addWidget(title);

    auto *hint = new QLabel(
        tr("拖动图片调整位置，滚轮或滑块缩放，左转/右转 90°。"
           "输出会按正方形裁剪。"));
    hint->setObjectName(QStringLiteral("PanelHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *canvasRow = new QHBoxLayout;
    canvasRow->addStretch();
    auto *canvasFrame = new QFrame;
    canvasFrame->setObjectName(QStringLiteral("ProjectCard"));
    canvasFrame->setAttribute(Qt::WA_StyledBackground, true);
    auto *canvasLayout = new QVBoxLayout(canvasFrame);
    canvasLayout->setContentsMargins(4, 4, 4, 4);
    m_canvas = new CoverCanvas(this);
    canvasLayout->addWidget(m_canvas);
    canvasRow->addWidget(canvasFrame);
    canvasRow->addStretch();
    layout->addLayout(canvasRow);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(8);

    auto *rotateLeft = new QPushButton(tr("左转 90°"));
    rotateLeft->setObjectName(QStringLiteral("GhostButton"));
    auto *rotateRight = new QPushButton(tr("右转 90°"));
    rotateRight->setObjectName(QStringLiteral("GhostButton"));
    auto *resetButton = new QPushButton(tr("重置"));
    resetButton->setObjectName(QStringLiteral("GhostButton"));
    controls->addWidget(rotateLeft);
    controls->addWidget(rotateRight);
    controls->addWidget(resetButton);
    controls->addSpacing(16);

    controls->addWidget(new QLabel(tr("缩放")));
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(100, 800);
    m_zoomSlider->setValue(100);
    m_zoomSlider->setFixedWidth(180);
    controls->addWidget(m_zoomSlider);
    m_zoomLabel = new QLabel(QStringLiteral("100%"));
    m_zoomLabel->setObjectName(QStringLiteral("Muted"));
    m_zoomLabel->setFixedWidth(46);
    controls->addWidget(m_zoomLabel);

    controls->addSpacing(16);
    controls->addWidget(new QLabel(tr("输出尺寸")));
    m_sizeCombo = new QComboBox;
    m_sizeCombo->setEditable(true);
    m_sizeCombo->addItems({QStringLiteral("64"), QStringLiteral("128"),
                           QStringLiteral("256"), QStringLiteral("512")});
    m_sizeCombo->setCurrentText(QString::number(m_outputSize));
    m_sizeCombo->setValidator(new QIntValidator(1, 4096, this));
    m_sizeCombo->setFixedWidth(110);
    controls->addWidget(m_sizeCombo);
    auto *sizeHint = new QLabel(tr("最大 4096"));
    sizeHint->setObjectName(QStringLiteral("Muted"));
    controls->addWidget(sizeHint);
    controls->addStretch();
    layout->addLayout(controls);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &CoverEditorDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(rotateLeft, &QPushButton::clicked, this, [this] { rotateBy(-90); });
    connect(rotateRight, &QPushButton::clicked, this, [this] { rotateBy(90); });
    connect(resetButton, &QPushButton::clicked, this, [this] {
        m_rotation = 0;
        m_zoom = 1.0;
        m_offset = QPointF();
        m_zoomSlider->blockSignals(true);
        m_zoomSlider->setValue(100);
        m_zoomSlider->blockSignals(false);
        updateZoomLabel();
        m_canvas->update();
    });
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int value) {
        m_zoom = value / 100.0;
        clampOffset();
        updateZoomLabel();
        m_canvas->update();
    });

    clampOffset();
    updateZoomLabel();
}

QSize CoverEditorDialog::rotatedSourceSize() const
{
    if (m_rotation % 180 == 0)
        return m_source.size();
    return QSize(m_source.height(), m_source.width());
}

double CoverEditorDialog::baseScale(const QSize &target) const
{
    const QSize rotated = rotatedSourceSize();
    if (rotated.isEmpty() || target.isEmpty())
        return 1.0;
    return qMax(target.width() / double(rotated.width()),
                target.height() / double(rotated.height()));
}

void CoverEditorDialog::clampOffset()
{
    const QSize rotated = rotatedSourceSize();
    const double scale = baseScale(QSize(m_canvasSize, m_canvasSize)) * m_zoom;
    const double maxX = qMax(0.0, (rotated.width() * scale - m_canvasSize) / 2.0);
    const double maxY = qMax(0.0, (rotated.height() * scale - m_canvasSize) / 2.0);
    m_offset.setX(qBound(-maxX, m_offset.x(), maxX));
    m_offset.setY(qBound(-maxY, m_offset.y(), maxY));
}

void CoverEditorDialog::paintImage(QPainter &painter, const QSize &target) const
{
    if (m_source.isNull())
        return;
    const double scale = baseScale(target) * m_zoom;
    const double ratio = target.width() / double(m_canvasSize);
    const QPointF center(target.width() / 2.0, target.height() / 2.0);

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.translate(center + m_offset * ratio);
    painter.rotate(m_rotation);
    painter.scale(scale, scale);
    painter.translate(-m_source.width() / 2.0, -m_source.height() / 2.0);
    painter.drawImage(QPointF(0, 0), m_source);
    painter.restore();
}

void CoverEditorDialog::paintPreview(QPainter &painter, const QSize &target) const
{
    const Theme::Colors colors = Theme::colors();
    constexpr int cell = 14;
    for (int y = 0; y < target.height(); y += cell) {
        for (int x = 0; x < target.width(); x += cell) {
            const bool light = ((x / cell) + (y / cell)) % 2 == 0;
            painter.fillRect(x, y, cell, cell,
                             light ? colors.checkerA : colors.checkerB);
        }
    }
    paintImage(painter, target);
}

void CoverEditorDialog::zoomBy(double factor)
{
    m_zoom = qBound(1.0, m_zoom * factor, 8.0);
    clampOffset();
    m_zoomSlider->blockSignals(true);
    m_zoomSlider->setValue(int(std::lround(m_zoom * 100.0)));
    m_zoomSlider->blockSignals(false);
    updateZoomLabel();
}

void CoverEditorDialog::panBy(const QPoint &delta)
{
    m_offset += QPointF(delta);
    clampOffset();
}

void CoverEditorDialog::rotateBy(int degrees)
{
    m_rotation = (m_rotation + degrees) % 360;
    if (m_rotation < 0)
        m_rotation += 360;
    clampOffset();
    m_canvas->update();
}

void CoverEditorDialog::updateZoomLabel()
{
    m_zoomLabel->setText(QStringLiteral("%1%").arg(int(std::lround(m_zoom * 100.0))));
}

QImage CoverEditorDialog::renderOutput(int size) const
{
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    paintImage(painter, QSize(size, size));
    painter.end();
    return image;
}

void CoverEditorDialog::accept()
{
    bool ok = false;
    int size = m_sizeCombo->currentText().toInt(&ok);
    if (!ok || size < 1)
        size = 512;
    m_outputSize = qBound(1, size, 4096);
    m_sizeCombo->setCurrentText(QString::number(m_outputSize));
    m_result = renderOutput(m_outputSize);
    QDialog::accept();
}

} // namespace mcstudio
