#include "ui/ImageDocumentWidget.h"

#include "ui/Theme.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QVBoxLayout>

namespace mcstudio {
namespace {

class ImagePreview : public QWidget
{
public:
    explicit ImagePreview(const QImage &image, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_image(image)
    {
        const int longest = qMax(1, qMax(image.width(), image.height()));
        m_scale = qBound(1, 512 / longest, 16);
        setMinimumSize(sizeHint());
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        if (m_image.isNull())
            return QSize(320, 200);
        return QSize(m_image.width() * m_scale + 32,
                     m_image.height() * m_scale + 32);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme::Colors colors = Theme::colors();
        QPainter painter(this);
        painter.fillRect(rect(), colors.base);
        if (m_image.isNull())
            return;

        const QSize targetSize(m_image.width() * m_scale, m_image.height() * m_scale);
        const QRect target((width() - targetSize.width()) / 2,
                           (height() - targetSize.height()) / 2,
                           targetSize.width(), targetSize.height());

        const int checker = 12;
        for (int y = 0; y < target.height(); y += checker) {
            for (int x = 0; x < target.width(); x += checker) {
                const bool dark = ((x / checker) + (y / checker)) % 2 == 0;
                painter.fillRect(QRect(target.left() + x, target.top() + y,
                                       qMin(checker, target.width() - x),
                                       qMin(checker, target.height() - y)),
                                 dark ? colors.checkerA : colors.checkerB);
            }
        }

        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(target, m_image);
        painter.setPen(colors.borderStrong);
        painter.drawRect(target.adjusted(0, 0, -1, -1));
    }

    void changeEvent(QEvent *event) override
    {
        QWidget::changeEvent(event);
        if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
            update();
    }

private:
    QImage m_image;
    int m_scale = 1;
};

} // namespace

ImageDocumentWidget::ImageDocumentWidget(const QString &title, const QString &relativePath,
                                         const QByteArray &data, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    const QImage image = QImage::fromData(data);

    auto *header = new QFrame;
    header->setObjectName(QStringLiteral("DocumentHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 9, 16, 9);
    headerLayout->setSpacing(10);

    auto *name = new QLabel(title);
    name->setObjectName(QStringLiteral("DocumentTitle"));
    const QString displayPath = relativePath == title ? tr("项目根目录")
                                                       : relativePath;
    auto *path = new QLabel(displayPath);
    path->setObjectName(QStringLiteral("DocumentPath"));
    path->setToolTip(relativePath);

    auto *badge = new QLabel;
    badge->setObjectName(QStringLiteral("DocumentBadge"));
    if (image.isNull())
        badge->setText(tr("无法解码 PNG"));
    else
        badge->setText(tr("PNG · %1 × %2").arg(image.width()).arg(image.height()));

    headerLayout->addWidget(name);
    headerLayout->addWidget(path, 1);
    headerLayout->addWidget(badge);
    layout->addWidget(header);

    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("ImageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setAlignment(Qt::AlignCenter);
    scroll->setFrameShape(QFrame::NoFrame);

    if (image.isNull()) {
        auto *error = new QLabel(tr("这张图片无法用 Qt 解码，可能是损坏或不支持的 PNG。"));
        error->setObjectName(QStringLiteral("PanelHint"));
        error->setAlignment(Qt::AlignCenter);
        scroll->setWidget(error);
    } else {
        scroll->setWidget(new ImagePreview(image));
    }

    layout->addWidget(scroll, 1);
}

} // namespace mcstudio
