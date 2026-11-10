#include "ui/CollectionWindow.h"

#include "core/AssetNamespace.h"
#include "core/AssetStack.h"
#include "core/ModCatalog.h"
#include "core/Project.h"
#include "core/TextureCategory.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace mcstudio {

CollectionWindow::CollectionWindow(const Project &project,
                                   const QVector<TextureBoardEntry> &entries,
                                   const QString &initialCategory,
                                   QWidget *parent)
    : QMainWindow(parent)
    , m_project(project)
    , m_projectName(project.name)
    , m_entries(entries)
    , m_categoryKey(initialCategory.isEmpty() ? QStringLiteral("all") : initialCategory)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("集合模式 — %1").arg(m_projectName));
    resize(1400, 900);
    setMinimumSize(960, 640);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QFrame;
    header->setObjectName(QStringLiteral("CollectionHeader"));
    auto *headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(18, 12, 18, 10);
    headerLayout->setSpacing(8);

    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(10);
    auto *title = new QLabel(tr("集合模式"));
    title->setObjectName(QStringLiteral("BoardTitle"));
    auto *projectLabel = new QLabel(m_projectName);
    projectLabel->setObjectName(QStringLiteral("Muted"));
    m_projectLabel = projectLabel;
    auto *hint = new QLabel(
        tr("双指缩放/平移 · Ctrl+滚轮 · 中键拖动 · 1:1 · 名称可悬停 · 双击图片编辑"));
    hint->setObjectName(QStringLiteral("BoardHint"));

    auto *categoryLabel = new QLabel(tr("类型"));
    categoryLabel->setObjectName(QStringLiteral("FieldLabel"));
    m_typeButton = new QToolButton;
    m_typeButton->setObjectName(QStringLiteral("GhostButton"));
    m_typeButton->setPopupMode(QToolButton::InstantPopup);
    m_typeButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_typeButton->setMinimumWidth(110);
    m_typeButton->setText(tr("类型：全部"));

    auto *modLabel = new QLabel(tr("模组"));
    modLabel->setObjectName(QStringLiteral("FieldLabel"));
    m_modButton = new QToolButton;
    m_modButton->setObjectName(QStringLiteral("GhostButton"));
    m_modButton->setPopupMode(QToolButton::InstantPopup);
    m_modButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_modButton->setMinimumWidth(110);
    m_modButton->setText(tr("模组：全部"));

    m_referenceCheck = new QCheckBox(tr("参考材质"));
    m_referenceCheck->setChecked(false);
    for (const TextureBoardEntry &entry : m_entries) {
        if (entry.referenceOnly) {
            m_referenceCheck->setChecked(true);
            break;
        }
    }
    m_referenceCheck->setToolTip(
        tr("蓝框 = 包内材质，橙框 = 参考 jar 中未覆盖的材质"));

    auto *groupLabel = new QLabel(tr("分组"));
    groupLabel->setObjectName(QStringLiteral("FieldLabel"));
    m_groupCombo = new QComboBox;
    m_groupCombo->addItem(tr("按类型"), QStringLiteral("type"));
    m_groupCombo->addItem(tr("按方块/物品"), QStringLiteral("association"));
    m_groupCombo->addItem(tr("按模组"), QStringLiteral("mod"));
    m_groupCombo->addItem(tr("按模组 + 方块/物品"), QStringLiteral("mod-association"));
    m_groupCombo->setMinimumWidth(150);

    auto *settingsButton = new QPushButton(tr("设置"));
    settingsButton->setObjectName(QStringLiteral("GhostButton"));
    settingsButton->setCheckable(true);

    auto *fitButton = new QPushButton(tr("适应窗口"));
    fitButton->setObjectName(QStringLiteral("GhostButton"));

    m_zoomLabel = new QLabel(QStringLiteral("500%"));
    m_zoomLabel->setObjectName(QStringLiteral("Muted"));
    m_zoomLabel->setMinimumWidth(40);
    m_zoomLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    topRow->addWidget(title);
    topRow->addWidget(projectLabel);
    topRow->addWidget(hint);
    topRow->addStretch();
    topRow->addWidget(categoryLabel);
    topRow->addWidget(m_typeButton);
    topRow->addWidget(modLabel);
    topRow->addWidget(m_modButton);
    topRow->addWidget(m_referenceCheck);
    topRow->addWidget(groupLabel);
    topRow->addWidget(m_groupCombo);
    topRow->addWidget(settingsButton);
    topRow->addWidget(fitButton);
    topRow->addWidget(m_zoomLabel);
    headerLayout->addLayout(topRow);
    layout->addWidget(header);

    m_settingsPanel = new QFrame;
    m_settingsPanel->setObjectName(QStringLiteral("CollectionSettings"));
    auto *settingsLayout = new QHBoxLayout(m_settingsPanel);
    settingsLayout->setContentsMargins(18, 7, 18, 7);
    settingsLayout->setSpacing(8);
    auto *widthLabel = new QLabel(tr("画布宽度"));
    widthLabel->setObjectName(QStringLiteral("FieldLabel"));
    m_widthSpin = new QSpinBox;
    m_widthSpin->setRange(4, 512);
    m_widthSpin->setValue(32);
    m_widthSpin->setSuffix(tr(" 方块"));
    m_widthSpin->setToolTip(tr("默认 32 方块宽；高度不限制"));
    m_detectedLabel = new QLabel(tr("按 1:1 像素显示：16× 材质占 1 方块，32× 占 2 方块"));
    m_detectedLabel->setObjectName(QStringLiteral("PanelHint"));
    settingsLayout->addWidget(widthLabel);
    settingsLayout->addWidget(m_widthSpin);
    settingsLayout->addWidget(m_detectedLabel);
    settingsLayout->addStretch();
    m_settingsPanel->setVisible(false);
    layout->addWidget(m_settingsPanel);

    m_canvas = new CollectionCanvas;
    m_canvas->onZoomChanged = [this](int percent) {
        m_zoomLabel->setText(QStringLiteral("%1%").arg(percent));
    };
    m_canvas->onTextureSelected = [this](const QString &path) {
        if (onTextureSelected)
            onTextureSelected(path);
    };
    m_canvas->onOpenTexture = [this](const QString &path) {
        if (onOpenTexture)
            onOpenTexture(path);
    };
    layout->addWidget(m_canvas, 1);

    m_statusLabel = new QLabel;
    m_statusLabel->setObjectName(QStringLiteral("PanelHint"));
    m_statusLabel->setContentsMargins(18, 5, 18, 8);
    layout->addWidget(m_statusLabel);

    setCentralWidget(central);

    connect(m_groupCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        m_groupMode = m_groupCombo->currentData().toString();
        rebuild();
    });
    connect(m_referenceCheck, &QCheckBox::toggled, this, [this](bool) { rebuild(); });
    connect(fitButton, &QPushButton::clicked, this, [this] {
        m_canvas->fitAll();
    });
    connect(settingsButton, &QPushButton::toggled, this, [this](bool checked) {
        m_settingsPanel->setVisible(checked);
    });
    connect(m_widthSpin, &QSpinBox::valueChanged, this, [this](int) {
        rebuild();
    });

    buildFilterMenus();
    rebuild();
}

void CollectionWindow::setGroupMode(const QString &mode)
{
    const int index = m_groupCombo->findData(mode);
    if (index >= 0)
        m_groupCombo->setCurrentIndex(index);
}

void CollectionWindow::fitAll()
{
    if (m_canvas)
        m_canvas->fitAll();
}

void CollectionWindow::setNamespaceFilter(const QString &namespaceId, const QString &label)
{
    if (m_namespaceFilter == namespaceId && m_namespaceLabel == label)
        return;
    m_namespaceFilter = namespaceId;
    m_namespaceLabel = label;
    if (m_filtersInitialized && m_modButton) {
        m_selectedMods.clear();
        if (namespaceId.isEmpty()) {
            for (const TextureBoardEntry &entry : m_entries)
                m_selectedMods.insert(entry.namespaceId);
        } else {
            m_selectedMods.insert(namespaceId);
        }
        buildFilterMenus();
        updateFilterButtons();
    }
    if (m_projectLabel) {
        m_projectLabel->setText(
            namespaceId.isEmpty()
                ? m_projectName
                : tr("%1 · %2").arg(m_projectName,
                                    label.isEmpty() ? namespaceId : label));
    }
    rebuild();
}

void CollectionWindow::setEntries(const QVector<TextureBoardEntry> &entries)
{
    bool hadReferences = false;
    for (const TextureBoardEntry &entry : m_entries) {
        if (entry.referenceOnly) {
            hadReferences = true;
            break;
        }
    }
    const int oldModCount = [this] {
        QSet<QString> namespaces;
        for (const TextureBoardEntry &entry : m_entries)
            namespaces.insert(entry.namespaceId);
        return namespaces.size();
    }();
    const bool allModsSelected = m_selectedMods.size() == oldModCount;

    m_entries = entries;
    bool hasReferences = false;
    for (const TextureBoardEntry &entry : m_entries) {
        if (entry.referenceOnly) {
            hasReferences = true;
            break;
        }
    }
    if (hasReferences && !hadReferences && m_referenceCheck) {
        const QSignalBlocker blocker(m_referenceCheck);
        m_referenceCheck->setChecked(true);
    }
    if (allModsSelected) {
        for (const TextureBoardEntry &entry : m_entries)
            m_selectedMods.insert(entry.namespaceId);
    }
    m_associationBuilt = false;
    buildFilterMenus();
    updateFilterButtons();
    rebuild();
}

void CollectionWindow::buildFilterMenus()
{
    QStringList categories;
    for (const TextureCategoryInfo &info : textureCategories())
        categories.append(info.key);

    QSet<QString> modSet;
    QHash<QString, QString> modLabels;
    for (const TextureBoardEntry &entry : m_entries) {
        if (entry.namespaceId.isEmpty())
            continue;
        modSet.insert(entry.namespaceId);
        if (!modLabels.contains(entry.namespaceId) && !entry.modName.isEmpty())
            modLabels.insert(entry.namespaceId, entry.modName);
    }
    for (const QString &ns : m_namespaces) {
        if (!ns.isEmpty())
            modSet.insert(ns);
    }
    QStringList mods = modSet.values();
    mods.sort(Qt::CaseInsensitive);

    if (!m_filtersInitialized) {
        if (m_categoryKey.isEmpty() || m_categoryKey == QLatin1String("all")) {
            for (const QString &key : categories)
                m_selectedCategories.insert(key);
        } else {
            m_selectedCategories.insert(m_categoryKey);
        }
        if (m_namespaceFilter.isEmpty()) {
            for (const QString &ns : mods)
                m_selectedMods.insert(ns);
        } else {
            m_selectedMods.insert(m_namespaceFilter);
        }
        m_filtersInitialized = true;
    }

    const auto rebuildMenu = [this](QToolButton *button, const QString &title,
                                    const QStringList &keys,
                                    const std::function<QString(const QString &)> &labelFor,
                                    const std::function<void(const QString &, bool)> &toggle,
                                    const std::function<void(bool)> &setAll) {
        if (button->menu())
            button->menu()->deleteLater();
        auto *menu = new QMenu(this);
        QAction *all = menu->addAction(tr("全选"));
        QAction *none = menu->addAction(tr("全不选"));
        QObject::connect(all, &QAction::triggered, this, [setAll] { setAll(true); });
        QObject::connect(none, &QAction::triggered, this, [setAll] { setAll(false); });
        menu->addSeparator();
        for (const QString &key : keys) {
            QAction *action = menu->addAction(labelFor(key));
            action->setCheckable(true);
            const QSet<QString> &selected =
                button == m_typeButton ? m_selectedCategories : m_selectedMods;
            action->setChecked(selected.contains(key));
            QObject::connect(action, &QAction::toggled, this,
                             [toggle, key](bool checked) { toggle(key, checked); });
        }
        button->setMenu(menu);
        button->setToolTip(title);
    };

    rebuildMenu(
        m_typeButton, tr("按类型筛选"),
        categories,
        [](const QString &key) { return textureCategoryLabel(key); },
        [this](const QString &key, bool checked) {
            if (checked)
                m_selectedCategories.insert(key);
            else
                m_selectedCategories.remove(key);
            updateFilterButtons();
            rebuild();
        },
        [this](bool selectAll) {
            m_selectedCategories.clear();
            if (selectAll) {
                for (const TextureCategoryInfo &info : textureCategories())
                    m_selectedCategories.insert(info.key);
            }
            QTimer::singleShot(0, this, [this] {
                buildFilterMenus();
                updateFilterButtons();
                rebuild();
            });
        });

    rebuildMenu(
        m_modButton, tr("按模组筛选"),
        mods,
        [this, modLabels](const QString &key) {
            return modLabels.value(key, modTitle(key, key));
        },
        [this](const QString &key, bool checked) {
            if (checked)
                m_selectedMods.insert(key);
            else
                m_selectedMods.remove(key);
            updateFilterButtons();
            rebuild();
        },
        [this, mods](bool selectAll) {
            m_selectedMods.clear();
            if (selectAll) {
                for (const QString &ns : mods)
                    m_selectedMods.insert(ns);
            }
            QTimer::singleShot(0, this, [this] {
                buildFilterMenus();
                updateFilterButtons();
                rebuild();
            });
        });

    updateFilterButtons();
}

void CollectionWindow::updateFilterButtons()
{
    const int typeTotal = textureCategories().size();
    if (m_selectedCategories.isEmpty()) {
        m_typeButton->setText(tr("类型：无"));
    } else if (m_selectedCategories.size() >= typeTotal) {
        m_typeButton->setText(tr("类型：全部"));
    } else {
        m_typeButton->setText(tr("类型：%1 项").arg(m_selectedCategories.size()));
    }

    QSet<QString> allMods;
    for (const TextureBoardEntry &entry : m_entries)
        allMods.insert(entry.namespaceId);
    for (const QString &ns : m_namespaces)
        allMods.insert(ns);
    if (m_selectedMods.isEmpty()) {
        m_modButton->setText(tr("模组：无"));
    } else if (m_selectedMods.size() >= allMods.size()) {
        m_modButton->setText(tr("模组：全部"));
    } else {
        m_modButton->setText(tr("模组：%1 项").arg(m_selectedMods.size()));
    }

    QStringList modNames;
    for (const QString &ns : m_selectedMods)
        modNames.append(modTitle(ns, ns));
    modNames.sort(Qt::CaseInsensitive);
    m_modButton->setToolTip(modNames.join(QStringLiteral("、")));
}

bool CollectionWindow::matchesFilters(const TextureBoardEntry &entry) const
{
    if (m_referenceCheck && !m_referenceCheck->isChecked() && entry.referenceOnly)
        return false;
    const QString category = entry.categoryKey.isEmpty()
                                 ? textureCategoryForPath(entry.path)
                                 : entry.categoryKey;
    if (!m_selectedCategories.contains(category))
        return false;
    if (!m_selectedMods.contains(entry.namespaceId))
        return false;
    return true;
}

void CollectionWindow::appendReferenceGroups(QVector<CollectionGroupData> &groups) const
{
    if (!m_referenceCheck || !m_referenceCheck->isChecked())
        return;

    QSet<QString> shown;
    for (const CollectionGroupData &group : groups) {
        for (const TextureBoardEntry &entry : group.entries)
            shown.insert(entry.path);
    }

    QHash<QString, QVector<TextureBoardEntry>> byMod;
    for (const TextureBoardEntry &entry : m_entries) {
        if (!entry.referenceOnly || shown.contains(entry.path) || !matchesFilters(entry))
            continue;
        byMod[entry.namespaceId].append(entry);
    }
    QStringList keys = byMod.keys();
    keys.sort(Qt::CaseInsensitive);
    for (const QString &ns : keys) {
        CollectionGroupData group;
        group.title = tr("参考材质 · %1").arg(modTitle(ns, ns));
        group.subtitle = tr("%1 个参考材质（未导入）").arg(byMod.value(ns).size());
        group.framed = false;
        group.showEntryNames = true;
        group.entries = byMod.value(ns);
        groups.append(group);
    }
}


void CollectionWindow::setReferenceContext(std::shared_ptr<const AssetStack> assets,
                                           std::shared_ptr<const ModCatalog> catalog,
                                           const QStringList &namespaces)
{
    const int oldModCount = [this] {
        QSet<QString> namespacesSet;
        for (const TextureBoardEntry &entry : m_entries)
            namespacesSet.insert(entry.namespaceId);
        return namespacesSet.size();
    }();
    const bool allModsSelected = m_selectedMods.size() == oldModCount;

    m_assets = std::move(assets);
    m_catalog = std::move(catalog);
    m_namespaces = namespaces;
    if (allModsSelected) {
        for (const QString &ns : m_namespaces)
            m_selectedMods.insert(ns);
    }
    m_sharedAssociation.reset();
    m_associationBuilt = false;
    m_associationPending = m_assets != nullptr;
    buildFilterMenus();
    rebuild();
}

void CollectionWindow::setAssociationResult(
    std::shared_ptr<const AssociationResult> association)
{
    m_sharedAssociation = std::move(association);
    m_associationBuilt = false;
    m_associationPending = false;
    rebuild();
}

void CollectionWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (!m_canvas)
        return;
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::PaletteChange)
        rebuild();
}

void CollectionWindow::rebuild()
{
    m_canvas->setCanvasBlocks(m_widthSpin->value());

    // Names are useful in every grouping mode, not only "按方块/物品".
    // Only resolve once a reference context or shared result exists; the
    // constructor must stay responsive for large packs.
    if (m_assets || m_sharedAssociation)
        ensureAssociationBuilt();

    QVector<CollectionGroupData> groups;
    if (m_groupMode == QLatin1String("association"))
        groups = buildAssociationGroups();
    else if (m_groupMode == QLatin1String("mod"))
        groups = buildModGroups();
    else if (m_groupMode == QLatin1String("mod-association"))
        groups = buildModAssociationGroups();
    else
        groups = buildTypeGroups();

    if (m_groupMode == QLatin1String("association")
        || m_groupMode == QLatin1String("mod-association")) {
        appendReferenceGroups(groups);
    }

    applyTextureNames(groups);
    m_canvas->setGroups(groups);

    int images = 0;
    int referenceImages = 0;
    for (const CollectionGroupData &group : groups) {
        images += group.entries.size();
        for (const TextureBoardEntry &entry : group.entries) {
            if (entry.referenceOnly)
                ++referenceImages;
        }
    }
    const QString filterPrefix = m_namespaceLabel.isEmpty()
                                     ? QString()
                                     : tr("模组：%1 · ").arg(m_namespaceLabel);
    if (groups.isEmpty()) {
        m_statusLabel->setText(filterPrefix + tr("当前筛选下没有材质"));
    } else {
        m_statusLabel->setText(
            filterPrefix
            + tr("%1 组 · %2 个材质（参考 %3） · 1:1 像素 · 画布宽 %4 方块 · 间距 4")
                  .arg(groups.size())
                  .arg(images)
                  .arg(referenceImages)
                  .arg(m_widthSpin->value()));
    }

    if (m_associationPending)
        m_statusLabel->setText(tr("正在后台解析方块名称，完成后自动刷新…"));
}

void CollectionWindow::applyTextureNames(QVector<CollectionGroupData> &groups) const
{
    if (!m_associationBuilt)
        return;

    QHash<QString, QString> names = m_association.textureNames;
    QHash<QString, QString> ids = m_association.textureIds;

    // Older caches only carry groups; reconstruct pack names from them so a
    // cached project still shows localized labels after upgrading.
    if (names.isEmpty()) {
        for (const TextureGroup &group : m_association.groups) {
            const QString id =
                QStringLiteral("%1:%2").arg(group.namespaceId, group.name);
            for (const QString &texture : group.textures) {
                if (!names.contains(texture)) {
                    names.insert(texture, group.displayName);
                    ids.insert(texture, id);
                }
            }
        }
    }

    for (CollectionGroupData &group : groups) {
        for (TextureBoardEntry &entry : group.entries) {
            entry.displayName = names.value(entry.path);
            entry.displayId = ids.value(entry.path);
        }
    }
}

int CollectionWindow::detectResolution(const Project &project) const
{
    const QRegularExpression pattern(QStringLiteral("(\\d{2,3})\\s*[xX]"));
    const auto match = pattern.match(project.description);
    if (match.hasMatch()) {
        const int declared = match.captured(1).toInt();
        if (declared >= 8 && declared <= 512)
            return declared;
    }

    QHash<int, int> counts;
    for (const TextureBoardEntry &entry : m_entries) {
        const QImage &image = entry.frames.isEmpty() ? entry.image : entry.frames.first();
        if (image.isNull() || image.width() != image.height())
            continue;
        const int size = image.width();
        if (size >= 8 && size <= 512 && (size & (size - 1)) == 0)
            ++counts[size];
    }

    int detected = 16;
    int best = 0;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > best || (it.value() == best && it.key() < detected)) {
            best = it.value();
            detected = it.key();
        }
    }
    return detected;
}

QVector<CollectionGroupData> CollectionWindow::buildTypeGroups() const
{
    QVector<CollectionGroupData> groups;
    const auto appendGroup = [&](const QString &key, const QString &title) {
        CollectionGroupData group;
        group.title = title;
        group.framed = false;
        group.showEntryNames = true;
        for (const TextureBoardEntry &entry : m_entries) {
            if (!matchesFilters(entry))
                continue;
            const QString category = entry.categoryKey.isEmpty()
                                         ? textureCategoryForPath(entry.path)
                                         : entry.categoryKey;
            if (category != key)
                continue;
            group.entries.append(entry);
        }
        if (!group.entries.isEmpty())
            groups.append(group);
    };

    for (const TextureCategoryInfo &info : textureCategories()) {
        if (m_selectedCategories.contains(info.key))
            appendGroup(info.key, info.label);
    }
    return groups;
}

bool CollectionWindow::ensureAssociationBuilt()
{
    if (m_associationBuilt)
        return true;
    if (m_sharedAssociation) {
        m_association = *m_sharedAssociation;
        m_associationBuilt = true;
        m_associationPending = false;
        return true;
    }
    if (m_assets) {
        // The main window builds this in the background; returning false keeps
        // the UI responsive and shows a pending status.
        m_associationPending = true;
        return false;
    }
    m_association = TextureAssociation::build(m_project);
    m_associationBuilt = true;
    return true;
}

QString CollectionWindow::modTitle(const QString &namespaceId,
                                   const QString &fallback) const
{
    if (m_catalog) {
        if (const NamespaceMod *mapping = m_catalog->entry(namespaceId)) {
            QString title = mapping->label();
            if (!mapping->found)
                title += tr("（未找到模组）");
            return title;
        }
    }
    if (!fallback.isEmpty() && fallback != namespaceId)
        return fallback;
    return namespaceId;
}

QString CollectionWindow::modelSourceLabel(const QString &modelSource) const
{
    if (modelSource == QLatin1String("reference"))
        return tr("参考实例");
    if (modelSource == QLatin1String("mixed"))
        return tr("资源包 + 参考实例");
    return tr("资源包");
}

QVector<CollectionGroupData> CollectionWindow::buildModGroups() const
{
    QHash<QString, QString> titles;
    QHash<QString, QVector<const TextureBoardEntry *>> entriesByMod;

    for (const TextureBoardEntry &entry : m_entries) {
        if (!matchesFilters(entry))
            continue;

        const QString key = entry.modId.isEmpty() ? entry.namespaceId : entry.modId;
        if (!titles.contains(key))
            titles.insert(key, modTitle(entry.namespaceId, entry.modName));
        entriesByMod[key].append(&entry);
    }

    QVector<CollectionGroupData> groups;
    for (auto it = entriesByMod.constBegin(); it != entriesByMod.constEnd(); ++it) {
        CollectionGroupData group;
        group.title = titles.value(it.key());
        group.subtitle = tr("%1 个材质").arg(it.value().size());
        group.framed = false;
        group.showEntryNames = true;
        for (const TextureBoardEntry *entry : it.value())
            group.entries.append(*entry);
        groups.append(group);
    }

    std::sort(groups.begin(), groups.end(),
              [](const CollectionGroupData &a, const CollectionGroupData &b) {
                  return a.title.localeAwareCompare(b.title) < 0;
              });
    return groups;
}

QVector<CollectionGroupData> CollectionWindow::buildModAssociationGroups()
{
    if (!m_selectedCategories.contains(QStringLiteral("block"))
        && !m_selectedCategories.contains(QStringLiteral("item"))) {
        return buildModGroups();
    }
    if (!ensureAssociationBuilt())
        return {};

    QHash<QString, int> entryIndex;
    for (int index = 0; index < m_entries.size(); ++index)
        entryIndex.insert(m_entries.at(index).path, index);

    QVector<CollectionGroupData> groups;
    for (const TextureGroup &association : m_association.groups) {
        if (!m_selectedCategories.contains(association.kind))
            continue;
        if (!m_selectedMods.contains(association.namespaceId))
            continue;

        CollectionGroupData group;
        group.title = QStringLiteral("%1 · %2")
                          .arg(modTitle(association.namespaceId, association.modName),
                               association.displayName);
        group.subtitle = tr("%1 · 模型：%2")
                             .arg(association.kind == QLatin1String("block") ? tr("方块")
                                                                             : tr("物品"),
                                  modelSourceLabel(association.modelSource));
        group.framed = true;
        for (const QString &path : association.textures) {
            const int index = entryIndex.value(path, -1);
            if (index < 0)
                continue;
            group.entries.append(m_entries.at(index));
        }
        if (!group.entries.isEmpty())
            groups.append(group);
    }

    std::sort(groups.begin(), groups.end(),
              [](const CollectionGroupData &a, const CollectionGroupData &b) {
                  return a.title.localeAwareCompare(b.title) < 0;
              });

    if (groups.isEmpty())
        return buildModGroups();
    return groups;
}

QVector<CollectionGroupData> CollectionWindow::buildAssociationGroups()
{
    if (!m_selectedCategories.contains(QStringLiteral("block"))
        && !m_selectedCategories.contains(QStringLiteral("item"))) {
        return buildTypeGroups();
    }

    if (!ensureAssociationBuilt())
        return {};

    QHash<QString, int> entryIndex;
    for (int index = 0; index < m_entries.size(); ++index)
        entryIndex.insert(m_entries.at(index).path, index);

    QVector<CollectionGroupData> groups;
    for (const TextureGroup &association : m_association.groups) {
        if (!m_selectedCategories.contains(association.kind))
            continue;
        if (!m_selectedMods.contains(association.namespaceId))
            continue;

        CollectionGroupData group;
        group.title = association.displayName;
        group.framed = true;
        for (const QString &path : association.textures) {
            const int index = entryIndex.value(path, -1);
            if (index < 0)
                continue;
            const TextureBoardEntry &entry = m_entries.at(index);
            if (!matchesFilters(entry))
                continue;
            group.entries.append(entry);
        }
        if (!group.entries.isEmpty())
            groups.append(group);
    }

    if (groups.isEmpty())
        return buildTypeGroups();
    return groups;
}

} // namespace mcstudio
