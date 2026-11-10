#include "ui/NewProjectDialog.h"

#include "core/AppPaths.h"
#include "core/PackFormat.h"

#include <QBuffer>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace mcstudio {

NewProjectDialog::NewProjectDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("新建资源包项目"));
    resize(620, 560);
    setMinimumSize(560, 500);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);

    auto *title = new QLabel(tr("新建资源包项目"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    layout->addWidget(title);

    auto *hint = new QLabel(
        tr("创建一个空资源包。软件会生成 pack.mcmeta，并把它放在 %1 下。")
            .arg(QDir::toNativeSeparators(AppPaths::projectsRoot())));
    hint->setObjectName(QStringLiteral("PanelHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(8);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);

    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText(tr("例如：我的材质包"));
    form->addRow(tr("项目名称"), m_nameEdit);

    m_versionCombo = new QComboBox;
    for (const PackFormatInfo &info : packFormatTable()) {
        m_versionCombo->addItem(
            tr("Java %1（pack_format %2）").arg(info.canonicalVersion).arg(info.format),
            info.format);
    }
    const int defaultIndex = m_versionCombo->findData(15);
    m_versionCombo->setCurrentIndex(defaultIndex >= 0 ? defaultIndex : 0);
    form->addRow(tr("Minecraft 版本"), m_versionCombo);

    m_descriptionEdit = new QLineEdit;
    m_descriptionEdit->setPlaceholderText(tr("资源包列表里显示的介绍，可留空"));
    form->addRow(tr("描述"), m_descriptionEdit);

    auto *iconRow = new QWidget;
    auto *iconLayout = new QHBoxLayout(iconRow);
    iconLayout->setContentsMargins(0, 0, 0, 0);
    iconLayout->setSpacing(8);
    m_iconPreview = new QLabel(tr("无"));
    m_iconPreview->setFixedSize(72, 72);
    m_iconPreview->setAlignment(Qt::AlignCenter);
    m_iconPreview->setObjectName(QStringLiteral("IconPreview"));
    m_chooseIconButton = new QPushButton(tr("选择图片…"));
    m_chooseIconButton->setObjectName(QStringLiteral("GhostButton"));
    m_clearIconButton = new QPushButton(tr("清除"));
    m_clearIconButton->setObjectName(QStringLiteral("GhostButton"));
    m_clearIconButton->setEnabled(false);
    iconLayout->addWidget(m_iconPreview);
    iconLayout->addWidget(m_chooseIconButton);
    iconLayout->addWidget(m_clearIconButton);
    iconLayout->addStretch();
    form->addRow(tr("封面（可选）"), iconRow);

    layout->addLayout(form);

    auto *location = new QLabel(
        tr("保存位置：%1").arg(QDir::toNativeSeparators(AppPaths::projectsRoot())));
    location->setObjectName(QStringLiteral("PanelHint"));
    location->setWordWrap(true);
    layout->addWidget(location);

    auto *previewLabel = new QLabel(tr("pack.mcmeta 预览"));
    previewLabel->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(previewLabel);

    m_preview = new QPlainTextEdit;
    m_preview->setReadOnly(true);
    m_preview->setObjectName(QStringLiteral("DocumentText"));
    m_preview->setMinimumHeight(120);
    layout->addWidget(m_preview, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_createButton = buttons->addButton(tr("创建并打开"), QDialogButtonBox::AcceptRole);
    m_createButton->setObjectName(QStringLiteral("PrimaryButton"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_createButton, &QPushButton::clicked, this, [this] {
        const QString name = m_nameEdit->text().trimmed();
        if (name.isEmpty())
            return;
        m_request.name = name;
        m_request.packFormat = m_versionCombo->currentData().toInt();
        m_request.supportedFormats.clear();
        m_request.description = m_descriptionEdit->text().trimmed().isEmpty()
                                    ? name
                                    : m_descriptionEdit->text().trimmed();
        updatePreview();
        accept();
    });
    connect(m_chooseIconButton, &QPushButton::clicked, this,
            &NewProjectDialog::chooseIcon);
    connect(m_clearIconButton, &QPushButton::clicked, this,
            &NewProjectDialog::clearIcon);
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString &) {
        updatePreview();
        updateCreateEnabled();
    });
    connect(m_descriptionEdit, &QLineEdit::textChanged, this,
            [this](const QString &) { updatePreview(); });
    connect(m_versionCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { updatePreview(); });

    updatePreview();
    updateCreateEnabled();
    m_nameEdit->setFocus();
}

void NewProjectDialog::chooseIcon()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择资源包封面"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty())
        return;

    QImage image(path);
    if (image.isNull()) {
        m_iconPreview->setText(tr("无法读取"));
        return;
    }

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return;

    m_request.packPng = png;
    m_iconPreview->setPixmap(
        QPixmap::fromImage(image).scaled(72, 72, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
    m_clearIconButton->setEnabled(true);
}

void NewProjectDialog::clearIcon()
{
    m_request.packPng.clear();
    m_iconPreview->setPixmap(QPixmap());
    m_iconPreview->setText(tr("无"));
    m_clearIconButton->setEnabled(false);
}

void NewProjectDialog::updatePreview()
{
    const int format = m_versionCombo->currentData().toInt();
    const QString name = m_nameEdit->text().trimmed();
    const QString description =
        m_descriptionEdit->text().trimmed().isEmpty() ? name : m_descriptionEdit->text().trimmed();
    m_preview->setPlainText(QString::fromUtf8(
        ProjectService::buildPackMeta(format, {}, description)));
}

void NewProjectDialog::updateCreateEnabled()
{
    m_createButton->setEnabled(!m_nameEdit->text().trimmed().isEmpty());
}

} // namespace mcstudio
