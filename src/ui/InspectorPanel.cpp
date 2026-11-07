#include "ui/InspectorPanel.h"

#include "core/Project.h"
#include "core/TextureCategory.h"
#include "ui/Theme.h"

#include <QDateTime>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace mcstudio {
namespace {

QLabel *fieldLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("FieldLabel"));
    return label;
}

QLabel *fieldValue()
{
    auto *label = new QLabel(QStringLiteral("—"));
    label->setObjectName(QStringLiteral("Muted"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
}

} // namespace

InspectorPanel::InspectorPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    auto *title = new QLabel(tr("检查器"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    m_badge = new QLabel(tr("未选择"));
    m_badge->setObjectName(QStringLiteral("Badge"));
    m_badge->setAttribute(Qt::WA_StyledBackground, true);
    headerRow->addWidget(title);
    headerRow->addStretch();
    headerRow->addWidget(m_badge);
    layout->addLayout(headerRow);

    auto *previewFrame = new QFrame;
    previewFrame->setObjectName(QStringLiteral("PreviewFrame"));
    previewFrame->setFixedHeight(176);
    auto *previewLayout = new QVBoxLayout(previewFrame);
    previewLayout->setContentsMargins(1, 1, 1, 1);
    m_preview = new QLabel;
    m_preview->setObjectName(QStringLiteral("InspectorPreview"));
    m_preview->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_preview);
    layout->addWidget(previewFrame);

    auto *propertyLabel = new QLabel(tr("属性"));
    propertyLabel->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(propertyLabel);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(8);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    const auto addField = [&](const QString &name) {
        auto *value = fieldValue();
        form->addRow(fieldLabel(name), value);
        return value;
    };
    m_type = addField(tr("类型"));
    m_size = addField(tr("尺寸"));
    m_resolution = addField(tr("分辨率"));
    m_source = addField(tr("来源"));
    m_referenceSource = addField(tr("参考来源"));
    m_animation = addField(tr("动画"));
    m_path = addField(tr("路径"));
    layout->addLayout(form);

    layout->addWidget(fieldLabel(tr("引用关系")));
    m_reference = new QLabel(tr("在卡片墙或集合模式中点击材质查看属性。"));
    m_reference->setObjectName(QStringLiteral("PanelHint"));
    m_reference->setWordWrap(true);
    layout->addWidget(m_reference);

    layout->addStretch(1);

    m_importButton = new QPushButton(tr("导入到资源包"));
    m_importButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_importButton->setEnabled(false);
    m_importButton->setToolTip(tr("把当前参考材质复制到 pack/ 覆盖层"));
    connect(m_importButton, &QPushButton::clicked, this, [this] {
        if (!m_currentPath.isEmpty() && onImportReference)
            onImportReference(m_currentPath);
    });
    layout->addWidget(m_importButton);

    auto *compare = new QPushButton(tr("与原版对比"));
    compare->setObjectName(QStringLiteral("GhostButton"));
    compare->setEnabled(false);
    auto *exportOne = new QPushButton(tr("导出此资源"));
    exportOne->setObjectName(QStringLiteral("GhostButton"));
    exportOne->setEnabled(false);
    layout->addWidget(compare);
    layout->addWidget(exportOne);

    m_timer = new QTimer(this);
    m_timer->setInterval(30);
    connect(m_timer, &QTimer::timeout, this, &InspectorPanel::tickPreview);

    clear();
}

void InspectorPanel::setTexture(const Project &project, const QString &relativePath)
{
    if (relativePath.isEmpty() || !project.exists(relativePath)) {
        clear();
        return;
    }

    const ProjectFile file = project.fileIndex().value(relativePath);
    const QString sourceLabel = file.source == QLatin1String("pack")
                                    ? tr("输出覆盖层 pack")
                                    : tr("解包源 source");
    setTextureBytes(relativePath, project.readBytes(relativePath),
                    project.readBytes(relativePath + QLatin1String(".mcmeta")),
                    sourceLabel, false);
}

void InspectorPanel::setTextureBytes(const QString &relativePath,
                                     const QByteArray &bytes,
                                     const QByteArray &mcmeta,
                                     const QString &sourceLabel, bool reference)
{
    const QImage image = QImage::fromData(bytes);
    if (image.isNull()) {
        clear();
        return;
    }

    m_animationMeta = AnimationMeta();
    m_frames.clear();
    m_frame = 0;
    m_elapsedMs = 0;

    m_animationMeta = AnimationMeta::fromMcmeta(mcmeta, image.size());
    if (m_animationMeta.valid && m_animationMeta.frames.size() > 1
        && m_animationMeta.frameHeight > 0 && m_animationMeta.physicalFrames > 0) {
        const int physical = m_animationMeta.physicalFrames;
        const int frameHeight = m_animationMeta.frameHeight;
        for (const AnimationFrame &frame : m_animationMeta.frames) {
            const int index = qBound(0, frame.index, physical - 1);
            m_frames.append(image.copy(0, index * frameHeight, image.width(), frameHeight));
        }
        m_timer->start();
    } else {
        m_frames.append(image);
        m_timer->stop();
    }

    const QString category = textureCategoryLabel(textureCategoryForPath(relativePath));
    const int width = image.width();
    const int height = image.height();
    const int resolution = width == height ? width : qMin(width, height);

    setField(m_type, tr("材质 · %1").arg(category));
    setField(m_size, QStringLiteral("%1 × %2").arg(width).arg(height));
    setField(m_resolution, QStringLiteral("%1×").arg(resolution));
    setField(m_source, sourceLabel);
    setField(m_animation,
             m_animationMeta.valid
                 ? tr("动图 · %1 帧 · frametime %2")
                       .arg(m_frames.size())
                       .arg(m_animationMeta.frametime)
                 : tr("静态"));
    setField(m_path, relativePath);
    m_path->setToolTip(relativePath);
    m_currentPath = relativePath;
    m_importButton->setEnabled(reference);
    m_importButton->setToolTip(
        reference ? tr("把当前参考材质复制到 pack/ 覆盖层")
                  : tr("当前材质已经在资源包内"));
    setReferenceInfo(QString());
    if (reference) {
        m_badge->setText(tr("参考材质"));
        m_reference->setText(
            tr("这是参考实例里的材质，尚未复制到资源包。\n"
               "右键卡片或点击“导入到资源包”可复制。"));
    } else {
        m_badge->setText(tr("已选择"));
        m_reference->setText(
            tr("双击卡片可打开大图；集合模式中按方块/物品分组查看关联材质。"));
    }
    updatePreview();
}

void InspectorPanel::setReferenceInfo(const QString &text)
{
    if (m_referenceSource)
        setField(m_referenceSource, text.isEmpty() ? QStringLiteral("—") : text);
}

void InspectorPanel::setAssociationHint(const QString &text)
{
    if (m_reference)
        m_reference->setText(text);
}

void InspectorPanel::clear()
{
    if (m_timer)
        m_timer->stop();
    m_frames.clear();
    m_animationMeta = AnimationMeta();
    m_frame = 0;
    m_elapsedMs = 0;

    if (m_badge)
        m_badge->setText(tr("未选择"));
    m_currentPath.clear();
    if (m_importButton) {
        m_importButton->setEnabled(false);
        m_importButton->setToolTip(tr("选择参考材质后可导入到资源包"));
    }
    for (QLabel *label : {m_type, m_size, m_resolution, m_source, m_referenceSource,
                          m_animation, m_path}) {
        setField(label, QStringLiteral("—"));
    }
    if (m_reference)
        m_reference->setText(tr("在卡片墙或集合模式中点击材质查看属性。"));
    updatePreview();
}

void InspectorPanel::setField(QLabel *label, const QString &value)
{
    if (label)
        label->setText(value);
}

void InspectorPanel::tickPreview()
{
    if (m_frames.size() < 2)
        return;
    m_elapsedMs += 30;
    bool changed = false;
    int guard = 0;
    while (m_elapsedMs >= qMax(1, m_animationMeta.frames.value(m_frame).time) * 50
           && guard++ < 1000) {
        m_elapsedMs -= qMax(1, m_animationMeta.frames.value(m_frame).time) * 50;
        m_frame = (m_frame + 1) % m_frames.size();
        changed = true;
    }
    if (changed)
        updatePreview();
}

void InspectorPanel::updatePreview()
{
    const Theme::Colors colors = Theme::colors();
    const int width = 240;
    const int height = 150;
    QPixmap pixmap(width * 2, height * 2);
    pixmap.setDevicePixelRatio(2.0);

    QPainter painter(&pixmap);
    painter.fillRect(QRectF(0, 0, width, height), colors.checkerA);
    for (int y = 0; y < height; y += 10) {
        for (int x = 0; x < width; x += 10) {
            if (((x / 10) + (y / 10)) % 2 == 0)
                painter.fillRect(QRectF(x, y, 10, 10), colors.checkerB);
        }
    }

    if (m_frames.isEmpty()) {
        painter.setPen(colors.textMuted);
        painter.drawText(QRectF(0, 0, width, height), Qt::AlignCenter,
                         tr("点击材质查看检查器"));
    } else {
        const QImage &image = m_frames.value(m_frame);
        const QSizeF target = image.size().scaled(QSize(width - 24, height - 24),
                                                  Qt::KeepAspectRatio);
        const QRectF rect((width - target.width()) / 2.0,
                          (height - target.height()) / 2.0,
                          target.width(), target.height());
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawImage(rect, image);
        painter.setPen(colors.borderStrong);
        painter.drawRect(rect.adjusted(0, 0, -1, -1));
    }

    m_preview->setPixmap(pixmap);
}

void InspectorPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if ((event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        && m_preview) {
        updatePreview();
    }
}

} // namespace mcstudio
