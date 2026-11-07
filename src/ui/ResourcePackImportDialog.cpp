#include "ui/ResourcePackImportDialog.h"

#include "core/GameEnvironment.h"
#include "core/GameSettings.h"
#include "core/PathUtils.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace mcstudio {
namespace {

QString formatBytes(qint64 bytes)
{
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

} // namespace

ResourcePackImportDialog::ResourcePackImportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("从游戏目录导入资源包"));
    resize(780, 520);
    setMinimumSize(620, 420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("游戏目录中的资源包"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    layout->addWidget(title);

    auto *hint = new QLabel(
        tr("列出首选项里已添加的 .minecraft 根目录和实例的 resourcepacks/*.zip；"
           "目录形式的资源包暂不支持。"));
    hint->setObjectName(QStringLiteral("PanelHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("搜索资源包 / 实例…"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({tr("资源包"), tr("实例"), tr("MC 版本"), tr("大小")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(0, Qt::AscendingOrder);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree, 1);

    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("PanelHint"));
    layout->addWidget(m_status);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_importButton = buttons->addButton(tr("导入"), QDialogButtonBox::AcceptRole);
    m_importButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_importButton->setEnabled(false);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_importButton, &QPushButton::clicked, this, [this] {
        if (!m_selectedPath.isEmpty())
            accept();
    });
    connect(m_search, &QLineEdit::textChanged, this,
            [this](const QString &) { applyFilter(); });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &ResourcePackImportDialog::updateSelection);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item)
                    return;
                m_selectedPath = item->data(0, Qt::UserRole).toString();
                if (!m_selectedPath.isEmpty())
                    accept();
            });

    loadPacks();
}

void ResourcePackImportDialog::loadPacks()
{
    const GameSettings settings = loadGameSettings();
    GameEnvironmentSnapshot snapshot =
        GameEnvironment::loadCachedSnapshot(settings.roots);
    if (snapshot.instanceCount() == 0 && !settings.roots.isEmpty())
        snapshot = GameEnvironment::scanRoots(settings.roots, {});

    QSet<QString> seen;
    int packCount = 0;
    const bool multipleRoots = snapshot.roots.size() > 1;

    for (const GameRootInfo &root : snapshot.roots) {
        for (const InstanceInfo &instance : root.instances) {
            for (const ResourcePackFileInfo &pack : instance.resourcePacks) {
                if (pack.isDirectory
                    || !pack.fileName.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) {
                    continue;
                }
                const QString path = normalizedPath(pack.path);
                if (path.isEmpty() || seen.contains(path))
                    continue;
                seen.insert(path);

                auto *item = new QTreeWidgetItem(m_tree);
                item->setText(0, pack.fileName);
                item->setText(1, multipleRoots
                                    ? tr("%1 · %2").arg(instance.name, root.name)
                                    : instance.name);
                item->setText(2, instance.versionLabel());
                item->setData(3, Qt::DisplayRole, pack.size);
                item->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
                item->setData(0, Qt::UserRole, path);
                item->setToolTip(0, QDir::toNativeSeparators(path));
                item->setToolTip(1, QDir::toNativeSeparators(instance.instancePath));
                ++packCount;
            }
        }
    }

    if (packCount == 0) {
        m_status->setText(
            settings.roots.isEmpty()
                ? tr("尚未添加游戏目录（编辑 → 首选项 → 游戏目录）。")
                : tr("没有在实例的 resourcepacks 目录中找到 ZIP 资源包。"));
    } else {
        m_status->setText(tr("找到 %1 个资源包").arg(packCount));
    }
}

void ResourcePackImportDialog::applyFilter()
{
    const QString filter = m_search->text().trimmed();
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_tree->topLevelItem(i);
        bool match = filter.isEmpty();
        if (!match) {
            for (int column = 0; column < 3 && !match; ++column)
                match = item->text(column).contains(filter, Qt::CaseInsensitive);
            if (!match)
                match = item->data(0, Qt::UserRole).toString().contains(filter, Qt::CaseInsensitive);
        }
        item->setHidden(!match);
    }
    if (m_tree->selectedItems().isEmpty())
        m_importButton->setEnabled(false);
}

void ResourcePackImportDialog::updateSelection()
{
    const QList<QTreeWidgetItem *> selected = m_tree->selectedItems();
    m_selectedPath = selected.isEmpty()
                         ? QString()
                         : selected.first()->data(0, Qt::UserRole).toString();
    m_importButton->setEnabled(!m_selectedPath.isEmpty());
}

} // namespace mcstudio
