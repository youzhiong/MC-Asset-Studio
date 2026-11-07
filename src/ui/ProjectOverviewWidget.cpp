#include "ui/ProjectOverviewWidget.h"

#include "core/PackFormat.h"

#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
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

QLabel *fieldValue(const QString &text, const QString &tooltip = {})
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("Muted"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    if (!tooltip.isEmpty())
        label->setToolTip(tooltip);
    return label;
}

QString packDescriptionText(const QJsonObject &root)
{
    const QJsonValue description =
        root.value(QStringLiteral("pack")).toObject().value(QStringLiteral("description"));
    if (description.isString())
        return description.toString();
    if (description.isObject())
        return QString::fromUtf8(
            QJsonDocument(description.toObject()).toJson(QJsonDocument::Compact));
    if (description.isArray())
        return QString::fromUtf8(
            QJsonDocument(description.toArray()).toJson(QJsonDocument::Compact));
    return {};
}

QVector<int> formatsFromValue(const QJsonValue &value)
{
    QVector<int> formats;
    if (value.isDouble()) {
        const int format = value.toInt();
        if (format > 0)
            formats.append(format);
    } else if (value.isArray()) {
        for (const QJsonValue &entry : value.toArray()) {
            const int format = entry.toInt();
            if (format > 0 && !formats.contains(format))
                formats.append(format);
        }
    } else if (value.isObject()) {
        const QJsonObject range = value.toObject();
        const int min = range.value(QStringLiteral("min_inclusive")).toInt();
        const int max = range.value(QStringLiteral("max_inclusive")).toInt(min);
        for (int format = min; format <= max && format - min < 256; ++format) {
            if (format > 0 && !formats.contains(format))
                formats.append(format);
        }
    }
    return formats;
}

QJsonValue descriptionValue(const QString &text)
{
    const QString trimmed = text.trimmed();
    if ((trimmed.startsWith(QLatin1Char('{')) && trimmed.endsWith(QLatin1Char('}')))
        || (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']')))) {
        const QJsonDocument document = QJsonDocument::fromJson(trimmed.toUtf8());
        if (document.isObject())
            return document.object();
        if (document.isArray())
            return document.array();
    }
    return text;
}

} // namespace

ProjectOverviewWidget::ProjectOverviewWidget(const Project &project, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(12);

    auto *eyebrow = new QLabel(tr("PROJECT OVERVIEW"));
    eyebrow->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(eyebrow);

    auto *title = new QLabel(project.name);
    title->setObjectName(QStringLiteral("EmptyTitle"));
    layout->addWidget(title);

    QString subtitleText;
    if (project.origin == QLatin1String("created")) {
        subtitleText = tr("这是一个新建的空资源包。source/ 保存 pack.mcmeta 等基础文件，"
                          "pack/ 是输出覆盖层；导出时重建为 ZIP。");
    } else if (project.origin == QLatin1String("folder")) {
        subtitleText = tr("文件夹已复制到软件数据目录，原文件夹不会被修改。"
                          "编辑结果位于 pack/ 覆盖层，导出时重建为 ZIP。");
    } else {
        subtitleText = tr("ZIP 已自动解包到软件数据目录。原包保持只读；编辑结果位于 "
                          "pack/ 覆盖层，导出时重建为新的 ZIP。");
    }
    auto *subtitle = new QLabel(subtitleText);
    subtitle->setObjectName(QStringLiteral("EmptyText"));
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("ProjectCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(18, 14, 18, 14);
    cardLayout->setSpacing(8);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(20);
    form->setVerticalSpacing(6);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    form->addRow(fieldLabel(tr("来源")),
                 fieldValue(project.sourceZip.isEmpty()
                                ? (project.origin == QLatin1String("created")
                                       ? tr("新建空资源包")
                                       : QDir::toNativeSeparators(project.originPath))
                                : QFileInfo(project.sourceZip).fileName(),
                            project.sourceZip.isEmpty() ? project.originPath
                                                        : project.sourceZip));
    form->addRow(fieldLabel(tr("包格式")),
                 fieldValue(tr("pack_format %1 · %2")
                                .arg(project.packFormat)
                                .arg(packFormatLabel(project.packFormat))));
    form->addRow(fieldLabel(tr("文件数")),
                 fieldValue(tr("%1 个文件").arg(project.fileCount)));
    form->addRow(fieldLabel(tr("解包目录")),
                 fieldValue(QDir::toNativeSeparators(project.sourceDir), project.sourceDir));
    form->addRow(fieldLabel(tr("输出覆盖层")),
                 fieldValue(QDir::toNativeSeparators(project.packDir), project.packDir));
    form->addRow(fieldLabel(tr("缓存")),
                 fieldValue(project.cacheManifestPath.isEmpty()
                                ? tr("项目级缓存（无 ZIP 指纹）")
                                : project.cacheManifestPath,
                            project.cacheManifestPath));

    cardLayout->addLayout(form);
    layout->addWidget(card);

    // pack.mcmeta form -----------------------------------------------------
    const QByteArray packMetaBytes = m_project.readBytes(QStringLiteral("pack.mcmeta"));
    QJsonParseError parseError;
    const QJsonDocument packMetaDocument =
        QJsonDocument::fromJson(packMetaBytes, &parseError);
    if (parseError.error == QJsonParseError::NoError && packMetaDocument.isObject())
        m_packMetaRoot = packMetaDocument.object();

    const QJsonObject pack =
        m_packMetaRoot.value(QStringLiteral("pack")).toObject();
    const int currentFormat = pack.value(QStringLiteral("pack_format")).toInt(project.packFormat);
    QVector<int> currentSupported =
        formatsFromValue(pack.value(QStringLiteral("supported_formats")));
    if (currentSupported.isEmpty())
        currentSupported = project.supportedFormats;

    auto *metaCard = new QFrame;
    metaCard->setObjectName(QStringLiteral("ProjectCard"));
    metaCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *metaLayout = new QVBoxLayout(metaCard);
    metaLayout->setContentsMargins(18, 14, 18, 14);
    metaLayout->setSpacing(8);

    auto *metaTitle = new QLabel(tr("pack.mcmeta"));
    metaTitle->setObjectName(QStringLiteral("SectionTitle"));
    metaLayout->addWidget(metaTitle);

    auto *metaHint = new QLabel(
        tr("这些字段会写入 pack/pack.mcmeta 覆盖层。未知字段会保留。"));
    metaHint->setObjectName(QStringLiteral("PanelHint"));
    metaHint->setWordWrap(true);
    metaLayout->addWidget(metaHint);

    auto *metaForm = new QFormLayout;
    metaForm->setHorizontalSpacing(20);
    metaForm->setVerticalSpacing(6);
    metaForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    metaForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    auto *formatRow = new QWidget;
    auto *formatLayout = new QHBoxLayout(formatRow);
    formatLayout->setContentsMargins(0, 0, 0, 0);
    formatLayout->setSpacing(8);
    m_formatSpin = new QSpinBox;
    m_formatSpin->setRange(1, 999);
    m_formatSpin->setValue(currentFormat);
    m_formatHint = new QLabel(packFormatLabel(currentFormat));
    m_formatHint->setObjectName(QStringLiteral("Muted"));
    formatLayout->addWidget(m_formatSpin);
    formatLayout->addWidget(m_formatHint);
    formatLayout->addStretch();
    metaForm->addRow(tr("pack_format"), formatRow);

    m_supportedEdit = new QLineEdit;
    QStringList supportedStrings;
    for (int format : currentSupported)
        supportedStrings.append(QString::number(format));
    m_supportedEdit->setText(supportedStrings.join(QStringLiteral(", ")));
    m_supportedEdit->setPlaceholderText(tr("留空表示只支持 pack_format；例如 15, 18, 22"));
    metaForm->addRow(tr("supported_formats"), m_supportedEdit);

    m_descriptionEdit = new QPlainTextEdit(packDescriptionText(m_packMetaRoot));
    m_descriptionEdit->setFixedHeight(58);
    m_descriptionEdit->setPlaceholderText(tr("资源包列表里显示的介绍文字"));
    metaForm->addRow(tr("description"), m_descriptionEdit);

    metaLayout->addLayout(metaForm);

    auto *previewLabel = new QLabel(tr("JSON 预览（只读）"));
    previewLabel->setObjectName(QStringLiteral("FieldLabel"));
    metaLayout->addWidget(previewLabel);

    m_jsonPreview = new QPlainTextEdit;
    m_jsonPreview->setReadOnly(true);
    m_jsonPreview->setObjectName(QStringLiteral("DocumentText"));
    m_jsonPreview->setFixedHeight(150);
    metaLayout->addWidget(m_jsonPreview);

    auto *saveRow = new QHBoxLayout;
    auto *saveButton = new QPushButton(tr("保存 pack.mcmeta"));
    saveButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_saveStatus = new QLabel;
    m_saveStatus->setObjectName(QStringLiteral("PanelHint"));
    saveRow->addWidget(saveButton);
    saveRow->addWidget(m_saveStatus);
    saveRow->addStretch();
    metaLayout->addLayout(saveRow);

    layout->addWidget(metaCard);

    connect(saveButton, &QPushButton::clicked, this, &ProjectOverviewWidget::savePackMeta);
    connect(m_formatSpin, &QSpinBox::valueChanged, this, [this](int value) {
        m_formatHint->setText(packFormatLabel(value));
        updatePreview();
    });
    connect(m_supportedEdit, &QLineEdit::textChanged, this,
            [this](const QString &) { updatePreview(); });
    connect(m_descriptionEdit, &QPlainTextEdit::textChanged, this,
            [this] { updatePreview(); });

    layout->addStretch(1);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(8);

    auto *packEditorButton = new QPushButton(tr("打开材质包编辑"));
    packEditorButton->setObjectName(QStringLiteral("GhostButton"));

    auto *exportButton = new QPushButton(tr("导出为新 ZIP"));
    exportButton->setObjectName(QStringLiteral("PrimaryButton"));
    auto *overwriteButton = new QPushButton(tr("覆盖原 ZIP"));
    overwriteButton->setObjectName(QStringLiteral("DangerButton"));
    overwriteButton->setEnabled(!project.sourceZip.isEmpty());
    overwriteButton->setToolTip(project.sourceZip.isEmpty()
                                    ? tr("新建/文件夹项目没有原始 ZIP，请使用“导出为新 ZIP”")
                                    : tr("用当前内容原子替换原 ZIP"));
    auto *folderButton = new QPushButton(tr("打开数据目录"));
    folderButton->setObjectName(QStringLiteral("GhostButton"));

    buttonRow->addWidget(packEditorButton);
    buttonRow->addWidget(exportButton);
    buttonRow->addWidget(overwriteButton);
    buttonRow->addWidget(folderButton);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    connect(packEditorButton, &QPushButton::clicked, this, [this] {
        if (onOpenPackEditor)
            onOpenPackEditor();
    });
    connect(exportButton, &QPushButton::clicked, this, [this] {
        if (onExportNew)
            onExportNew();
    });
    connect(overwriteButton, &QPushButton::clicked, this, [this] {
        if (onOverwrite)
            onOverwrite();
    });
    connect(folderButton, &QPushButton::clicked, this, [this] {
        if (onOpenFolder)
            onOpenFolder();
    });

    scroll->setWidget(content);
    outerLayout->addWidget(scroll);
    QTimer::singleShot(50, scroll, [scroll] {
        scroll->verticalScrollBar()->setValue(0);
    });

    updatePreview();
}

QVector<int> ProjectOverviewWidget::supportedFormatsFromEdit() const
{
    QVector<int> formats;
    const QStringList parts =
        m_supportedEdit->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")),
                                      Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        bool ok = false;
        const int value = part.toInt(&ok);
        if (ok && value > 0 && !formats.contains(value))
            formats.append(value);
    }
    return formats;
}

void ProjectOverviewWidget::updatePreview()
{
    QJsonObject root = m_packMetaRoot;
    QJsonObject pack = root.value(QStringLiteral("pack")).toObject();
    pack.insert(QStringLiteral("pack_format"), m_formatSpin->value());
    pack.insert(QStringLiteral("description"), descriptionValue(m_descriptionEdit->toPlainText()));
    const QVector<int> supported = supportedFormatsFromEdit();
    if (supported.isEmpty()) {
        pack.remove(QStringLiteral("supported_formats"));
    } else {
        QJsonArray array;
        for (int format : supported)
            array.append(format);
        pack.insert(QStringLiteral("supported_formats"), array);
    }
    root.insert(QStringLiteral("pack"), pack);
    m_jsonPreview->setPlainText(QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Indented)));
    m_formatHint->setText(packFormatLabel(m_formatSpin->value()));
}

void ProjectOverviewWidget::savePackMeta()
{
    applyPackMetaValues(m_formatSpin->value(), m_descriptionEdit->toPlainText(),
                        supportedFormatsFromEdit());
}

bool ProjectOverviewWidget::applyPackMetaValues(int packFormat,
                                                const QString &description,
                                                const QVector<int> &supportedFormats)
{
    if (packFormat <= 0)
        return false;

    QJsonObject root = m_packMetaRoot;
    QJsonObject pack = root.value(QStringLiteral("pack")).toObject();
    pack.insert(QStringLiteral("pack_format"), packFormat);
    pack.insert(QStringLiteral("description"), descriptionValue(description));
    if (supportedFormats.isEmpty()) {
        pack.remove(QStringLiteral("supported_formats"));
    } else {
        QJsonArray array;
        for (int format : supportedFormats) {
            if (format > 0)
                array.append(format);
        }
        if (array.isEmpty())
            pack.remove(QStringLiteral("supported_formats"));
        else
            pack.insert(QStringLiteral("supported_formats"), array);
    }
    root.insert(QStringLiteral("pack"), pack);

    const QString path = m_project.absoluteOutputPath(QStringLiteral("pack.mcmeta"));
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (m_saveStatus)
            m_saveStatus->setText(tr("保存失败：无法创建 pack/"));
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        if (m_saveStatus)
            m_saveStatus->setText(tr("保存失败：%1").arg(file.errorString()));
        return false;
    }

    m_packMetaRoot = root;
    if (m_saveStatus)
        m_saveStatus->setText(tr("已保存到 pack/pack.mcmeta"));
    if (onPackMetaSaved)
        onPackMetaSaved(packFormat, supportedFormats, description);
    return true;
}

} // namespace mcstudio
