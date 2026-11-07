#include "ui/TextureBoardPage.h"

#include "core/AppPaths.h"
#include "core/AssetNamespace.h"
#include "core/ModCatalog.h"
#include "core/Project.h"
#include "core/TextureCategory.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>

namespace mcstudio {

TextureBoardPage::TextureBoardPage(const Project &project,
                                   const AssetStack *referenceStack, bool showReference,
                                   QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_showReference(showReference)
{
    m_projectId = project.id;
    m_layoutPath = QDir(AppPaths::layoutsRoot())
                       .filePath(m_projectId + QStringLiteral(".json"));

    if (referenceStack) {
        m_referenceStack = *referenceStack;
        m_hasReferenceStack = m_referenceStack.providerCount() > 1;
    }

    loadEntries(project);

    int tileSize = 140;
    int zoom = 100;
    bool labels = true;
    bool animation = true;
    bool freeLayout = false;
    QHash<QString, QPointF> positions;
    loadLayout(tileSize, labels, animation, freeLayout, zoom, positions);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *header = new QFrame;
    header->setObjectName(QStringLiteral("TextureBoardHeader"));
    auto *headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(16, 10, 16, 10);
    headerLayout->setSpacing(7);

    auto *titleRow = new QHBoxLayout;
    titleRow->setSpacing(10);
    auto *title = new QLabel(tr("材质墙"));
    title->setObjectName(QStringLiteral("BoardTitle"));
    m_countLabel = new QLabel(tr("%1 个材质").arg(m_entries.size()));
    m_countLabel->setObjectName(QStringLiteral("Muted"));
    auto *hint = new QLabel(tr("中键拖动 · 双指平移 · Ctrl+滚轮缩放 · 双击打开"));
    hint->setObjectName(QStringLiteral("BoardHint"));
    m_zoomLabel = new QLabel(QStringLiteral("100%"));
    m_zoomLabel->setObjectName(QStringLiteral("Muted"));
    m_zoomLabel->setMinimumWidth(42);
    m_zoomLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *collectionButton = new QPushButton(tr("集合模式"));
    collectionButton->setObjectName(QStringLiteral("PrimaryButton"));
    collectionButton->setToolTip(tr("在独立窗口中打开集合画布"));
    connect(collectionButton, &QPushButton::clicked, this, [this] {
        if (onOpenCollection)
            onOpenCollection();
    });

    titleRow->addWidget(title);
    titleRow->addWidget(m_countLabel);
    titleRow->addSpacing(8);
    titleRow->addWidget(hint);
    titleRow->addStretch();
    titleRow->addWidget(collectionButton);
    titleRow->addWidget(m_zoomLabel);
    headerLayout->addLayout(titleRow);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(8);

    auto *sizeText = new QLabel(tr("卡片"));
    sizeText->setObjectName(QStringLiteral("FieldLabel"));
    m_tileSlider = new QSlider(Qt::Horizontal);
    m_tileSlider->setRange(96, 220);
    m_tileSlider->setValue(tileSize);
    m_tileSlider->setFixedWidth(110);
    m_sizeLabel = new QLabel(QString::number(tileSize));
    m_sizeLabel->setObjectName(QStringLiteral("Muted"));
    m_sizeLabel->setMinimumWidth(28);

    m_labelsCheck = new QCheckBox(tr("名称"));
    m_labelsCheck->setChecked(labels);
    m_animationCheck = new QCheckBox(tr("动画"));
    m_animationCheck->setChecked(animation);
    m_freeCheck = new QCheckBox(tr("自由布局"));
    m_freeCheck->setChecked(freeLayout);
    m_referenceCheck = new QCheckBox(tr("参考材质"));
    m_referenceCheck->setChecked(m_showReference && m_hasReferenceStack);
    m_referenceCheck->setEnabled(m_hasReferenceStack);
    m_referenceCheck->setToolTip(
        m_hasReferenceStack
            ? tr("显示参考实例中资源包没有覆盖的材质（橙色框）")
            : tr("当前没有可用的参考实例或 jar 资源"));
    m_sourceCombo = new QComboBox;
    m_sourceCombo->addItem(tr("全部材质"), QStringLiteral("all"));
    m_sourceCombo->addItem(tr("仅包内"), QStringLiteral("pack"));
    m_sourceCombo->addItem(tr("仅参考"), QStringLiteral("reference"));
    m_sourceCombo->setToolTip(tr("按来源筛选：包内材质 / 参考未导入材质"));

    auto *resetButton = new QPushButton(tr("重置布局"));
    resetButton->setObjectName(QStringLiteral("GhostButton"));
    auto *fitButton = new QPushButton(tr("适应窗口"));
    fitButton->setObjectName(QStringLiteral("GhostButton"));

    auto *zoomText = new QLabel(tr("缩放"));
    zoomText->setObjectName(QStringLiteral("FieldLabel"));
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(10, 400);
    m_zoomSlider->setSingleStep(10);
    m_zoomSlider->setPageStep(25);
    m_zoomSlider->setValue(zoom);
    m_zoomSlider->setFixedWidth(100);

    m_packLegend = new QLabel;
    m_packLegend->setObjectName(QStringLiteral("Muted"));
    m_packLegend->setToolTip(tr("资源包内已有（或已覆盖）的材质"));
    m_referenceLegend = new QLabel;
    m_referenceLegend->setObjectName(QStringLiteral("Muted"));
    m_referenceLegend->setToolTip(tr("参考实例中存在，但资源包内没有的材质"));
    updateThemeColors();

    controls->addWidget(sizeText);
    controls->addWidget(m_tileSlider);
    controls->addWidget(m_sizeLabel);
    controls->addSpacing(6);
    controls->addWidget(m_labelsCheck);
    controls->addWidget(m_animationCheck);
    controls->addWidget(m_freeCheck);
    controls->addWidget(m_referenceCheck);
    controls->addWidget(m_sourceCombo);
    controls->addSpacing(4);
    controls->addWidget(m_packLegend);
    controls->addWidget(m_referenceLegend);
    controls->addWidget(resetButton);
    controls->addWidget(fitButton);
    controls->addStretch();
    controls->addWidget(zoomText);
    controls->addWidget(m_zoomSlider);
    headerLayout->addLayout(controls);
    root->addWidget(header);

    m_view = new TextureBoardView;
    m_view->setEntries(m_entries, tileSize, labels, positions);
    m_view->setFreeLayout(freeLayout);
    m_view->setAnimationEnabled(animation);
    m_view->setZoomPercent(zoom);

    m_view->onOpenTexture = [this](const QString &path) {
        if (onOpenTexture)
            onOpenTexture(path);
    };
    m_view->onViewTexture = [this](const QString &path) {
        if (onViewTexture)
            onViewTexture(path);
    };
    m_view->onTextureSelected = [this](const QString &path) {
        if (onTextureSelected)
            onTextureSelected(path);
    };
    m_view->onImportTexture = [this](const QString &path) {
        if (onImportReference)
            onImportReference(path);
    };
    m_view->onImportNamespace = [this](const QString &namespaceId) {
        if (onImportReferenceNamespace)
            onImportReferenceNamespace(namespaceId);
    };
    m_view->onLayoutChanged = [this] { scheduleSave(); };
    m_view->onZoomChanged = [this](int percent) {
        m_zoomLabel->setText(QStringLiteral("%1%").arg(percent));
        QSignalBlocker blocker(m_zoomSlider);
        m_zoomSlider->setValue(percent);
    };

    connect(m_tileSlider, &QSlider::valueChanged, this, [this](int value) {
        m_view->setTileSize(value);
        m_sizeLabel->setText(QString::number(value));
        scheduleSave();
    });
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int value) {
        m_view->setZoomPercent(value);
        m_zoomLabel->setText(QStringLiteral("%1%").arg(value));
    });
    connect(m_labelsCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_view->setLabelsVisible(checked);
        scheduleSave();
    });
    connect(m_animationCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_view->setAnimationEnabled(checked);
        scheduleSave();
    });
    connect(m_freeCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_view->setFreeLayout(checked);
        scheduleSave();
    });
    connect(m_referenceCheck, &QCheckBox::toggled, this,
            &TextureBoardPage::setShowReferenceTextures);
    connect(m_sourceCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const QString source = m_sourceCombo->currentData().toString();
        if (source == QLatin1String("reference")
            && (!m_referenceCheck || !m_referenceCheck->isChecked())) {
            setShowReferenceTextures(true);
        }
        m_view->setSourceFilter(source);
        updateCountLabel();
    });
    connect(resetButton, &QPushButton::clicked, this, [this] {
        m_view->resetLayout();
        scheduleSave();
    });
    connect(fitButton, &QPushButton::clicked, this, [this] {
        m_view->fitBoard();
    });

    root->addWidget(m_view, 1);

    m_zoomLabel->setText(QStringLiteral("%1%").arg(m_view->zoomPercent()));
    updateCountLabel();

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(500);
    connect(m_saveTimer, &QTimer::timeout, this, &TextureBoardPage::saveLayout);

    if (m_showReference && m_hasReferenceStack)
        setShowReferenceTextures(true);
}

TextureBoardPage::~TextureBoardPage()
{
    saveLayout();
}

void TextureBoardPage::updateThemeColors()
{
    const Theme::Colors colors = Theme::colors();
    if (m_packLegend) {
        m_packLegend->setText(
            QStringLiteral("<span style=\"color:%1\">■</span> 包内")
                .arg(colors.pack.name()));
    }
    if (m_referenceLegend) {
        m_referenceLegend->setText(
            QStringLiteral("<span style=\"color:%1\">■</span> 参考未导入")
                .arg(colors.reference.name()));
    }
}

void TextureBoardPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        updateThemeColors();
}

void TextureBoardPage::setCategoryFilter(const QString &key)
{
    QString category = key;
    if (category.startsWith(QLatin1String("texture.")))
        category = category.mid(8);
    if (category.isEmpty())
        category = QStringLiteral("all");
    m_categoryFilter = category;
    if (m_view)
        m_view->setCategoryFilter(category);
    updateCountLabel();
}

void TextureBoardPage::setNamespaceFilter(const QString &namespaceId, const QString &label)
{
    m_namespaceFilter = namespaceId;
    m_modLabel = label;
    if (m_view)
        m_view->setNamespaceFilter(namespaceId);
    updateCountLabel();
}

void TextureBoardPage::setSearchFilter(const QString &text)
{
    if (m_view)
        m_view->setSearchFilter(text);
    updateCountLabel();
}

QVector<TextureBoardEntry> TextureBoardPage::entriesWithReferences()
{
    if (m_hasReferenceStack && !m_referenceLoaded)
        loadReferenceEntries();
    QVector<TextureBoardEntry> result = m_packEntries;
    result += m_referenceEntries;
    return result;
}

void TextureBoardPage::setModCatalog(const std::shared_ptr<const ModCatalog> &catalog)
{
    m_modCatalog = catalog;
    if (!m_modCatalog)
        return;

    const auto apply = [this](QVector<TextureBoardEntry> &entries) {
        for (TextureBoardEntry &entry : entries) {
            if (const NamespaceMod *mapping =
                    m_modCatalog->entry(entry.namespaceId)) {
                entry.modId = mapping->modId;
                entry.modName = mapping->label();
            }
        }
    };
    apply(m_packEntries);
    apply(m_referenceEntries);
    apply(m_entries);
}

void TextureBoardPage::setReferenceStack(const AssetStack &stack)
{
    m_referenceStack = stack;
    m_hasReferenceStack = m_referenceStack.providerCount() > 1;
    m_referenceLoaded = false;
    m_referenceEntries.clear();

    if (m_referenceCheck) {
        const QSignalBlocker blocker(m_referenceCheck);
        m_referenceCheck->setEnabled(m_hasReferenceStack);
        if (!m_hasReferenceStack)
            m_showReference = false;
        m_referenceCheck->setChecked(m_showReference);
        m_referenceCheck->setToolTip(
            m_hasReferenceStack
                ? tr("显示参考实例中资源包没有覆盖的材质（橙色框）")
                : tr("当前没有可用的参考实例或 jar 资源"));
    }

    if (m_showReference && m_hasReferenceStack)
        loadReferenceEntries();
    rebuildEntries();
}

void TextureBoardPage::setShowReferenceTextures(bool show)
{
    if (show && !m_hasReferenceStack)
        show = false;
    m_showReference = show;

    if (m_referenceCheck) {
        const QSignalBlocker blocker(m_referenceCheck);
        m_referenceCheck->setChecked(show);
    }
    if (show && !m_referenceLoaded)
        loadReferenceEntries();
    rebuildEntries();
}

void TextureBoardPage::refreshPackEntries(const Project &project)
{
    m_project = project;
    loadEntries(project);
    m_referenceLoaded = false;
    m_referenceEntries.clear();
    if (m_showReference && m_hasReferenceStack)
        loadReferenceEntries();
    rebuildEntries();
}

void TextureBoardPage::replaceTexture(const Project &project, const QString &path)
{
    m_project = project;

    const auto file = project.fileIndex().value(path);
    const QByteArray bytes = project.readBytes(path);
    const QByteArray mcmeta = project.readBytes(path + QStringLiteral(".mcmeta"));
    const TextureBoardEntry entry =
        buildEntry(path, bytes, mcmeta, file.source, false);
    if (entry.image.isNull())
        return;

    bool found = false;
    for (TextureBoardEntry &candidate : m_packEntries) {
        if (candidate.path == path) {
            candidate = entry;
            found = true;
            break;
        }
    }
    for (TextureBoardEntry &candidate : m_entries) {
        if (candidate.path == path) {
            candidate = entry;
            break;
        }
    }

    if (!found) {
        // A texture that was just copied from the reference stack: rebuild the
        // lists so it shows up as a pack card and disappears from REF.
        loadEntries(project);
        m_referenceLoaded = false;
        m_referenceEntries.clear();
        if (m_showReference && m_hasReferenceStack)
            loadReferenceEntries();
        rebuildEntries();
        return;
    }

    if (m_view)
        m_view->replaceEntry(entry);
    updateCountLabel();
}

void TextureBoardPage::rebuildEntries()
{
    QHash<QString, QPointF> positions;
    int tileSize = 140;
    bool labels = true;
    QString search;
    QString sourceFilter = QStringLiteral("all");
    if (m_view) {
        positions = m_view->positions();
        tileSize = m_view->tileSize();
        labels = m_view->labelsVisible();
        search = m_view->searchFilter();
        sourceFilter = m_view->sourceFilter();
    }

    m_entries = m_packEntries;
    if (m_showReference)
        m_entries += m_referenceEntries;

    if (m_view) {
        m_view->setEntries(m_entries, tileSize, labels, positions);
        m_view->setCategoryFilter(m_categoryFilter);
        m_view->setNamespaceFilter(m_namespaceFilter);
        m_view->setSearchFilter(search);
        m_view->setSourceFilter(sourceFilter);
    }
    updateCountLabel();
}

void TextureBoardPage::updateCountLabel()
{
    if (!m_countLabel)
        return;
    if (m_entries.isEmpty()) {
        m_countLabel->setText(tr("当前项目没有纹理"));
        return;
    }

    const int visible = m_view ? m_view->visibleCount() : 0;
    const int total = static_cast<int>(m_entries.size());
    int referenceCount = 0;
    for (const TextureBoardEntry &entry : m_entries) {
        if (entry.referenceOnly)
            ++referenceCount;
    }

    const QString prefix = m_modLabel.isEmpty() ? QString()
                                                : m_modLabel + QStringLiteral(" · ");
    QString suffix;
    if (referenceCount > 0)
        suffix = tr(" · 参考 %1").arg(referenceCount);
    if (visible == total)
        m_countLabel->setText(tr("%1%2 个材质%3").arg(prefix).arg(visible).arg(suffix));
    else
        m_countLabel->setText(
            tr("%1%2 / %3 个材质%4").arg(prefix).arg(visible).arg(total).arg(suffix));
}

TextureBoardEntry TextureBoardPage::buildEntry(const QString &path,
                                               const QByteArray &bytes,
                                               const QByteArray &mcmeta,
                                               const QString &source, bool referenceOnly,
                                               const QString &referenceLabel)
{
    TextureBoardEntry entry;
    const QImage image = QImage::fromData(bytes);
    if (image.isNull())
        return entry;

    entry.path = path;
    entry.namespaceId = namespaceFromAssetPath(path);
    entry.categoryKey = textureCategoryForPath(path);
    entry.modId = entry.namespaceId;
    entry.modName = entry.namespaceId;
    entry.image = image;
    entry.source = source;
    entry.referenceOnly = referenceOnly;
    entry.referenceLabel = referenceLabel;

    entry.animation = AnimationMeta::fromMcmeta(mcmeta, image.size());
    entry.animated = entry.animation.valid;
    if (entry.animated) {
        const int physical = entry.animation.physicalFrames;
        const int frameHeight = entry.animation.frameHeight;
        if (physical > 0 && frameHeight > 0) {
            entry.frames.reserve(entry.animation.frames.size());
            for (const AnimationFrame &frame : entry.animation.frames) {
                const int frameIndex = qBound(0, frame.index, physical - 1);
                entry.frames.append(
                    image.copy(0, frameIndex * frameHeight, image.width(), frameHeight));
            }
        } else {
            entry.animated = false;
            entry.animation = {};
        }
    }
    if (!entry.animated)
        entry.frames.append(image);
    return entry;
}

void TextureBoardPage::loadEntries(const Project &project)
{
    const QHash<QString, ProjectFile> &index = project.fileIndex();
    m_packEntries.clear();

    for (const QString &path : project.files()) {
        if (!path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)
            || !path.contains(QLatin1String("/textures/"))) {
            continue;
        }

        const QByteArray bytes = project.readBytes(path);
        const TextureBoardEntry entry =
            buildEntry(path, bytes, project.readBytes(path + QLatin1String(".mcmeta")),
                       index.value(path).source, false);
        if (!entry.image.isNull())
            m_packEntries.append(entry);
    }
    std::sort(m_packEntries.begin(), m_packEntries.end(),
              [](const TextureBoardEntry &a, const TextureBoardEntry &b) {
                  if (a.animated != b.animated)
                      return a.animated;
                  return a.path.localeAwareCompare(b.path) < 0;
              });

    m_entries = m_packEntries;
}

void TextureBoardPage::loadReferenceEntries()
{
    m_referenceEntries.clear();
    m_referenceLoaded = true;
    if (!m_hasReferenceStack)
        return;

    QStringList candidates;
    for (const QString &path : m_referenceStack.list(QStringLiteral("assets/"))) {
        if (!path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)
            || !path.contains(QLatin1String("/textures/"))) {
            continue;
        }
        if (m_project.fileIndex().contains(path))
            continue;
        candidates.append(path);
    }

    QProgressDialog progress(tr("正在加载参考材质…"), QString(), 0,
                             qMax(1, candidates.size()), this);
    progress.setWindowTitle(tr("参考材质"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    progress.setAutoClose(false);
    progress.setCancelButton(nullptr);
    if (candidates.size() > 200) {
        progress.setValue(0);
        QCoreApplication::processEvents();
    }

    int done = 0;
    for (const QString &path : candidates) {
        const QByteArray bytes = m_referenceStack.read(path);
        const AssetLocation location = m_referenceStack.locate(path);
        const TextureBoardEntry entry =
            buildEntry(path, bytes, m_referenceStack.read(path + QLatin1String(".mcmeta")),
                       QStringLiteral("reference"), true, location.label);
        if (!entry.image.isNull())
            m_referenceEntries.append(entry);

        ++done;
        if (candidates.size() > 200 && done % 25 == 0) {
            progress.setValue(done);
            progress.setLabelText(
                tr("正在加载参考材质 %1/%2").arg(done).arg(candidates.size()));
            QCoreApplication::processEvents();
        }
    }
    progress.close();

    std::sort(m_referenceEntries.begin(), m_referenceEntries.end(),
              [](const TextureBoardEntry &a, const TextureBoardEntry &b) {
                  if (a.animated != b.animated)
                      return a.animated;
                  return a.path.localeAwareCompare(b.path) < 0;
              });
}

void TextureBoardPage::loadLayout(int &tileSize, bool &labels, bool &animation,
                                  bool &freeLayout, int &zoom,
                                  QHash<QString, QPointF> &positions)
{
    QFile file(m_layoutPath);
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return;

    const QJsonObject root = document.object();
    tileSize = qBound(96, root.value(QStringLiteral("tileSize")).toInt(140), 220);
    labels = root.value(QStringLiteral("labels")).toBool(true);
    animation = root.value(QStringLiteral("animation")).toBool(true);
    freeLayout = root.value(QStringLiteral("freeLayout")).toBool(false);
    zoom = qBound(10, root.value(QStringLiteral("zoom")).toInt(100), 400);

    const QJsonObject savedPositions =
        root.value(QStringLiteral("positions")).toObject();
    for (auto it = savedPositions.constBegin(); it != savedPositions.constEnd(); ++it) {
        const QJsonArray array = it.value().toArray();
        if (array.size() == 2) {
            positions.insert(it.key(),
                             QPointF(array.at(0).toDouble(), array.at(1).toDouble()));
        }
    }
}

void TextureBoardPage::saveLayout()
{
    if (!m_view)
        return;

    QDir().mkpath(AppPaths::layoutsRoot());

    QJsonObject positions;
    const auto current = m_view->positions();
    for (auto it = current.constBegin(); it != current.constEnd(); ++it) {
        QJsonArray point;
        point.append(it.value().x());
        point.append(it.value().y());
        positions.insert(it.key(), point);
    }

    QJsonObject root;
    root.insert(QStringLiteral("tileSize"), m_view->tileSize());
    root.insert(QStringLiteral("labels"), m_view->labelsVisible());
    root.insert(QStringLiteral("animation"), m_view->animationEnabled());
    root.insert(QStringLiteral("freeLayout"), m_view->freeLayout());
    root.insert(QStringLiteral("zoom"), m_view->zoomPercent());
    root.insert(QStringLiteral("positions"), positions);

    QSaveFile file(m_layoutPath);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.commit();
}

void TextureBoardPage::scheduleSave()
{
    if (m_saveTimer)
        m_saveTimer->start();
}

} // namespace mcstudio
