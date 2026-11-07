#include "ui/GameDirectoryPage.h"

#include "ui/Theme.h"

#include "core/PathUtils.h"

#include <QAbstractItemView>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <utility>

namespace mcstudio {
namespace {

QFrame *makeDivider()
{
    auto *divider = new QFrame;
    divider->setObjectName(QStringLiteral("Divider"));
    divider->setFixedHeight(1);
    return divider;
}

QString formatTimestamp(const QString &isoUtc)
{
    if (isoUtc.isEmpty())
        return {};
    const QDateTime time = QDateTime::fromString(isoUtc, Qt::ISODate);
    if (!time.isValid())
        return {};
    return time.toLocalTime().toString(QStringLiteral("HH:mm:ss"));
}

} // namespace

GameDirectoryPage::GameDirectoryPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(120);
    connect(m_pollTimer, &QTimer::timeout, this, &GameDirectoryPage::pollScan);
    updateScanUi();
}

GameDirectoryPage::~GameDirectoryPage()
{
    m_scanTask.cancel();
}

void GameDirectoryPage::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(9);

    auto *titleRow = new QHBoxLayout;
    auto *title = new QLabel(tr("游戏目录"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    auto *hint = new QLabel(
        tr("支持多个 .minecraft 根目录；PCL / HMCL 版本隔离实例会自动识别。"));
    hint->setObjectName(QStringLiteral("PanelHint"));
    titleRow->addWidget(title);
    titleRow->addSpacing(8);
    titleRow->addWidget(hint);
    titleRow->addStretch();
    layout->addLayout(titleRow);

    m_rootList = new QListWidget;
    m_rootList->setObjectName(QStringLiteral("GameRootList"));
    m_rootList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_rootList->setAlternatingRowColors(false);
    m_rootList->setMinimumHeight(72);
    m_rootList->setMaximumHeight(94);
    layout->addWidget(m_rootList);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(8);
    m_addRootButton = new QPushButton(tr("添加目录…"));
    m_addRootButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_removeRootButton = new QPushButton(tr("移除"));
    m_removeRootButton->setObjectName(QStringLiteral("GhostButton"));
    m_scanButton = new QPushButton(tr("扫描"));
    m_scanButton->setObjectName(QStringLiteral("GhostButton"));
    m_cancelButton = new QPushButton(tr("取消"));
    m_cancelButton->setObjectName(QStringLiteral("GhostButton"));
    m_cancelButton->setEnabled(false);

    buttonRow->addWidget(m_addRootButton);
    buttonRow->addWidget(m_removeRootButton);
    buttonRow->addStretch();
    buttonRow->addWidget(m_cancelButton);
    buttonRow->addWidget(m_scanButton);
    layout->addLayout(buttonRow);

    auto *progressRow = new QHBoxLayout;
    progressRow->setSpacing(10);
    m_progressBar = new QProgressBar;
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(5);
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    m_scanStatusLabel = new QLabel(tr("尚未扫描"));
    m_scanStatusLabel->setObjectName(QStringLiteral("PanelHint"));
    m_scanStatusLabel->setMinimumWidth(260);
    progressRow->addWidget(m_progressBar, 1);
    progressRow->addWidget(m_scanStatusLabel);
    layout->addLayout(progressRow);

    layout->addWidget(makeDivider());

    auto *referenceTitle = new QLabel(tr("参考实例"));
    referenceTitle->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(referenceTitle);

    auto *referenceRow = new QHBoxLayout;
    referenceRow->setSpacing(14);
    m_autoRadio = new QRadioButton(tr("按 pack_format 自动匹配"));
    m_autoRadio->setToolTip(
        tr("读取资源包 pack.mcmeta 的 pack_format，选择 Minecraft 版本最接近的实例；"
           "无匹配时可在导入后手动选择。"));
    m_defaultRadio = new QRadioButton(tr("使用默认实例"));
    referenceRow->addWidget(m_autoRadio);
    referenceRow->addWidget(m_defaultRadio);
    referenceRow->addStretch();
    layout->addLayout(referenceRow);

    auto *defaultRow = new QHBoxLayout;
    defaultRow->setSpacing(8);
    auto *defaultLabel = new QLabel(tr("默认实例"));
    defaultLabel->setObjectName(QStringLiteral("FieldLabel"));
    m_defaultCombo = new QComboBox;
    m_defaultCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_defaultCombo->setMinimumContentsLength(46);
    m_defaultCombo->setToolTip(tr("自动匹配失败时使用；也可在项目里单独覆盖。"));
    defaultRow->addWidget(defaultLabel);
    defaultRow->addWidget(m_defaultCombo, 1);
    layout->addLayout(defaultRow);

    m_referenceHint = new QLabel(
        tr("参考实例只用于补全 blockstates / models / lang，不会把参考材质写入导出。"));
    m_referenceHint->setObjectName(QStringLiteral("PanelHint"));
    m_referenceHint->setWordWrap(true);
    layout->addWidget(m_referenceHint);

    layout->addWidget(makeDivider());

    auto *instanceHeader = new QHBoxLayout;
    instanceHeader->setSpacing(8);
    auto *instanceTitle = new QLabel(tr("实例"));
    instanceTitle->setObjectName(QStringLiteral("SectionTitle"));
    m_summaryLabel = new QLabel(tr("0 个实例"));
    m_summaryLabel->setObjectName(QStringLiteral("Muted"));
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(tr("搜索实例 / 版本 / 加载器…"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(260);
    instanceHeader->addWidget(instanceTitle);
    instanceHeader->addWidget(m_summaryLabel);
    instanceHeader->addStretch();
    instanceHeader->addWidget(m_searchEdit);
    layout->addLayout(instanceHeader);

    m_instanceTree = new QTreeWidget;
    m_instanceTree->setObjectName(QStringLiteral("InstanceTree"));
    m_instanceTree->setColumnCount(7);
    m_instanceTree->setHeaderLabels({tr("实例"), tr("MC 版本"), tr("加载器"), tr("mods"),
                                     tr("资源包"), tr("客户端 JAR"), tr("根目录")});
    m_instanceTree->setRootIsDecorated(false);
    m_instanceTree->setUniformRowHeights(true);
    m_instanceTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_instanceTree->setFrameShape(QFrame::NoFrame);
    m_instanceTree->setSortingEnabled(true);
    m_instanceTree->sortByColumn(0, Qt::AscendingOrder);
    m_instanceTree->header()->setStretchLastSection(false);
    m_instanceTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_instanceTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_instanceTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_instanceTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_instanceTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_instanceTree->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_instanceTree->header()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    layout->addWidget(m_instanceTree, 1);

    connect(m_rootList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *, QListWidgetItem *) { updateInstanceTable(); });
    connect(m_addRootButton, &QPushButton::clicked, this, &GameDirectoryPage::browseRoot);
    connect(m_removeRootButton, &QPushButton::clicked, this,
            &GameDirectoryPage::removeSelectedRoot);
    connect(m_scanButton, &QPushButton::clicked, this,
            [this] { startScan(true); });
    connect(m_cancelButton, &QPushButton::clicked, this, [this] {
        m_scanTask.cancel();
        m_scanStatusLabel->setText(tr("正在取消…"));
        m_cancelButton->setEnabled(false);
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this,
            [this](const QString &) { updateInstanceTable(); });
    connect(m_autoRadio, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_settings.referenceMode = QStringLiteral("auto");
            updateReferenceControls();
            notifyChanged();
        }
    });
    connect(m_defaultRadio, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_settings.referenceMode = QStringLiteral("default");
            updateReferenceControls();
            notifyChanged();
        }
    });
    connect(m_defaultCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        m_settings.defaultInstanceKey = m_defaultCombo->currentData().toString();
        notifyChanged();
    });
}

void GameDirectoryPage::setSettings(const GameSettings &settings)
{
    m_settings = settings;
    rebuildRootList();
    updateReferenceControls();
    updateReferenceCombo();
    updateInstanceTable();
    updateSummary();
    updateScanUi();
}

void GameDirectoryPage::rebuildRootList(const QString &selectPath)
{
    const QString keep = selectPath.isEmpty() ? selectedRootPath() : normalizedPath(selectPath);

    QSignalBlocker blocker(m_rootList);
    m_rootList->clear();

    auto *allItem = new QListWidgetItem(tr("全部根目录"));
    allItem->setData(Qt::UserRole, QString());
    allItem->setToolTip(tr("显示所有已添加目录下的实例"));
    m_rootList->addItem(allItem);

    for (const QString &rootPath : std::as_const(m_settings.roots)) {
        int instanceCount = 0;
        QString flags;
        if (const GameRootInfo *root = m_snapshot.findRoot(rootPath)) {
            instanceCount = root->instanceCount();
            flags = tr("versions %1 · assets %2 · mods 根目录 %3")
                        .arg(root->hasVersions ? tr("有") : tr("无"),
                             root->hasAssets ? tr("有") : tr("无"),
                             root->hasRootMods ? tr("有") : tr("无"));
        }

        const QString displayPath = QDir::toNativeSeparators(rootPath);
        auto *item = new QListWidgetItem(
            instanceCount > 0
                ? tr("%1  ·  %2 个实例").arg(displayPath).arg(instanceCount)
                : displayPath);
        item->setData(Qt::UserRole, rootPath);
        item->setToolTip(flags.isEmpty() ? displayPath
                                         : QStringLiteral("%1\n%2").arg(displayPath, flags));
        m_rootList->addItem(item);
    }

    int targetRow = 0;
    if (!keep.isEmpty()) {
        for (int row = 1; row < m_rootList->count(); ++row) {
            if (samePath(m_rootList->item(row)->data(Qt::UserRole).toString(), keep)) {
                targetRow = row;
                break;
            }
        }
    }
    m_rootList->setCurrentRow(targetRow);
    blocker.unblock();
    updateInstanceTable();
}

QString GameDirectoryPage::selectedRootPath() const
{
    const QListWidgetItem *item = m_rootList ? m_rootList->currentItem() : nullptr;
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void GameDirectoryPage::browseRoot()
{
    const QString initial = m_settings.recentRoots.isEmpty()
                                ? QDir::homePath()
                                : m_settings.recentRoots.first();
    const QString path = QFileDialog::getExistingDirectory(
        this, tr("选择 .minecraft 游戏目录"), initial);
    if (path.isEmpty())
        return;
    addRootPath(path);
}

void GameDirectoryPage::addRootPath(const QString &path)
{
    const QString normalized = normalizedPath(path);
    if (normalized.isEmpty()) {
        QMessageBox::warning(this, tr("无法添加目录"), tr("目录路径为空。"));
        return;
    }
    if (!QFileInfo(normalized).isDir()) {
        QMessageBox::warning(this, tr("无法添加目录"),
                             tr("目录不存在：\n%1").arg(normalized));
        return;
    }

    for (const QString &root : std::as_const(m_settings.roots)) {
        if (samePath(root, normalized)) {
            rebuildRootList(normalized);
            return;
        }
    }

    rememberGameRoot(m_settings, normalized);
    rebuildRootList(normalized);
    updateReferenceCombo();
    notifyChanged();
    startScan(false);
}

void GameDirectoryPage::removeSelectedRoot()
{
    const QString path = selectedRootPath();
    if (path.isEmpty()) {
        QMessageBox::information(this, tr("移除目录"),
                                 tr("请选择一个具体目录；「全部根目录」不能移除。"));
        return;
    }

    QStringList remaining;
    for (const QString &root : std::as_const(m_settings.roots)) {
        if (!samePath(root, path))
            remaining.append(root);
    }
    m_settings.roots = remaining;

    for (int index = static_cast<int>(m_snapshot.roots.size()) - 1; index >= 0; --index) {
        if (samePath(m_snapshot.roots.at(index).path, path))
            m_snapshot.roots.removeAt(index);
    }

    rebuildRootList();
    updateReferenceCombo();
    updateInstanceTable();
    updateSummary();
    m_scanStatusLabel->setText(tr("已移除目录（缓存保留，可在首选项中清除）"));
    notifyChanged();
}

void GameDirectoryPage::updateReferenceControls()
{
    const bool useDefault = m_settings.referenceMode == QLatin1String("default");
    {
        QSignalBlocker autoBlocker(m_autoRadio);
        QSignalBlocker defaultBlocker(m_defaultRadio);
        m_autoRadio->setChecked(!useDefault);
        m_defaultRadio->setChecked(useDefault);
    }
    m_defaultCombo->setEnabled(useDefault);
}

void GameDirectoryPage::updateReferenceCombo()
{
    QSignalBlocker blocker(m_defaultCombo);
    m_defaultCombo->clear();
    m_defaultCombo->addItem(tr("（未设置）"), QString());

    const bool multipleRoots = m_snapshot.roots.size() > 1;
    for (const GameRootInfo &root : m_snapshot.roots) {
        for (const InstanceInfo &instance : root.instances) {
            const QString label =
                multipleRoots
                    ? tr("%1 · %2 · %3 · %4")
                          .arg(instance.name, instance.versionLabel(),
                               instance.loaderLabel(), root.name)
                    : tr("%1 · %2 · %3").arg(instance.name, instance.versionLabel(),
                                             instance.loaderLabel());
            m_defaultCombo->addItem(label, instance.key);
        }
    }

    int index = m_defaultCombo->findData(m_settings.defaultInstanceKey);
    if (index < 0 && !m_settings.defaultInstanceKey.isEmpty()) {
        // Keep the stored key selectable even if the instance is currently
        // missing; the combo shows it as unavailable instead of silently
        // switching the user's default.
        m_defaultCombo->addItem(
            tr("（当前未扫描到）%1").arg(m_settings.defaultInstanceKey),
            m_settings.defaultInstanceKey);
        index = m_defaultCombo->count() - 1;
    }
    m_defaultCombo->setCurrentIndex(index >= 0 ? index : 0);
    blocker.unblock();
    m_defaultCombo->setEnabled(m_settings.referenceMode == QLatin1String("default"));
}

void GameDirectoryPage::updateInstanceTable()
{
    if (!m_instanceTree)
        return;

    const QString rootFilter = selectedRootPath();
    const QString filter = m_searchEdit->text().trimmed();

    m_instanceTree->setSortingEnabled(false);
    m_instanceTree->clear();

    for (const GameRootInfo &root : m_snapshot.roots) {
        if (!rootFilter.isEmpty() && !samePath(root.path, rootFilter))
            continue;
        for (const InstanceInfo &instance : root.instances) {
            if (!instanceMatches(instance, root.path, filter))
                continue;

            auto *item = new QTreeWidgetItem(m_instanceTree);
            item->setText(0, instance.name);
            item->setData(0, Qt::UserRole, instance.key);
            item->setToolTip(0, instance.instancePath);
            item->setText(1, instance.versionLabel());
            item->setText(2, instance.loaderLabel());
            item->setData(3, Qt::DisplayRole, instance.modCount());
            item->setData(4, Qt::DisplayRole, instance.resourcePackCount());
            item->setText(5, instance.jarLabel());
            item->setText(6, root.name);
            item->setToolTip(1, tr("assetIndex：%1").arg(
                                    instance.assetIndex.isEmpty() ? tr("未知")
                                                                  : instance.assetIndex));
            item->setToolTip(3, tr("根目录 mods：%1\n实例 mods：%2\n已禁用：%3")
                                    .arg(instance.rootModCount())
                                    .arg(instance.instanceModCount())
                                    .arg(instance.disabledModCount()));
            item->setToolTip(5, instance.hasClientJar ? instance.clientJar
                                                      : tr("未找到客户端 JAR"));
            item->setToolTip(6, root.path);

            if (!instance.hasClientJar) {
                item->setForeground(5, Theme::colors().warning);
            }
            if (!instance.warnings.isEmpty()) {
                QStringList warnings = instance.warnings;
                item->setToolTip(0, QStringLiteral("%1\n%2")
                                        .arg(instance.instancePath,
                                             warnings.join(QLatin1Char('\n'))));
                item->setForeground(0, Theme::colors().warning);
            }
        }
    }

    m_instanceTree->setSortingEnabled(true);
    m_instanceTree->sortByColumn(0, Qt::AscendingOrder);
    updateSummary();
}

bool GameDirectoryPage::instanceMatches(const InstanceInfo &instance, const QString &rootPath,
                                        const QString &filter) const
{
    if (filter.isEmpty())
        return true;
    const QStringList haystack = {
        instance.name,
        instance.clientVersion,
        instance.loader,
        instance.loaderVersion,
        instance.assetIndex,
        rootPath,
        instance.instancePath,
        instance.key,
    };
    for (const QString &value : haystack) {
        if (value.contains(filter, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

void GameDirectoryPage::startScan(bool forceRefresh)
{
    if (m_scanning)
        return;

    if (m_settings.roots.isEmpty()) {
        m_scanStatusLabel->setText(tr("请先添加 .minecraft 游戏目录"));
        updateSummary();
        return;
    }

    m_scanning = true;
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    m_scanStatusLabel->setText(tr("准备扫描…"));
    updateScanUi();

    GameScanOptions options;
    options.forceRefresh = forceRefresh;
    options.useCache = true;

    const QStringList roots = m_settings.roots;
    m_scanTask.start(
        roots, options,
        [this](int done, int total, const QString &message) {
            const int maximum = qMax(1, total);
            m_progressBar->setRange(0, maximum);
            m_progressBar->setValue(qBound(0, done, maximum));
            m_scanStatusLabel->setText(message);
        },
        [this](const GameEnvironmentSnapshot &snapshot, bool ok, const QString &error) {
            finishScan(snapshot, ok, error);
        });
    m_pollTimer->start();
}

void GameDirectoryPage::pollScan()
{
    m_scanTask.poll();
}

void GameDirectoryPage::finishScan(const GameEnvironmentSnapshot &snapshot, bool ok,
                                   const QString &error)
{
    m_pollTimer->stop();
    m_scanning = false;

    if (!ok) {
        m_scanStatusLabel->setText(tr("扫描失败：%1").arg(error));
        updateScanUi();
        return;
    }

    m_snapshot = snapshot;
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(1);

    QString status = tr("扫描完成 · %1 个根目录 · %2 个实例")
                         .arg(m_snapshot.roots.size())
                         .arg(m_snapshot.instanceCount());
    if (!m_snapshot.warnings.isEmpty())
        status = m_snapshot.warnings.join(tr("；"));

    QString latest;
    for (const GameRootInfo &root : m_snapshot.roots) {
        const QString stamp = formatTimestamp(root.scannedAt);
        if (!stamp.isEmpty())
            latest = stamp;
    }
    if (!latest.isEmpty())
        status += tr(" · %1").arg(latest);

    m_scanStatusLabel->setText(status);
    m_scanStatusLabel->setToolTip(m_snapshot.warnings.join(QLatin1Char('\n')));

    // Preserve the selected root while refreshing counts.
    rebuildRootList();
    updateReferenceCombo();
    updateInstanceTable();
    updateScanUi();

    if (onScanFinished)
        onScanFinished();
}

void GameDirectoryPage::updateSummary()
{
    if (!m_summaryLabel)
        return;

    int resourcePacks = 0;
    for (const GameRootInfo &root : m_snapshot.roots)
        resourcePacks += root.totalResourcePacks();

    if (m_snapshot.roots.isEmpty()) {
        m_summaryLabel->setText(tr("0 个实例"));
        return;
    }

    m_summaryLabel->setText(tr("%1 个根目录 · %2 个实例 · %3 个 mod · %4 个资源包")
                                .arg(m_snapshot.roots.size())
                                .arg(m_snapshot.instanceCount())
                                .arg(m_snapshot.modCount())
                                .arg(resourcePacks));
}

void GameDirectoryPage::updateScanUi()
{
    if (!m_scanButton)
        return;

    m_addRootButton->setEnabled(!m_scanning);
    m_removeRootButton->setEnabled(!m_scanning);
    m_scanButton->setEnabled(!m_scanning && !m_settings.roots.isEmpty());
    m_scanButton->setText(m_scanning ? tr("扫描中…") : tr("扫描 / 刷新"));
    m_cancelButton->setEnabled(m_scanning);
    m_progressBar->setVisible(m_scanning);
}

void GameDirectoryPage::notifyChanged()
{
    if (onSettingsChanged)
        onSettingsChanged();
}

} // namespace mcstudio
