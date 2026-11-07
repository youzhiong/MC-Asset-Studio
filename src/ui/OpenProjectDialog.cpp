#include "ui/OpenProjectDialog.h"

#include "core/AppPaths.h"
#include "core/PackFormat.h"
#include "core/ProjectService.h"

#include <QAbstractItemView>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace mcstudio {
namespace {

QString originLabel(const QString &origin)
{
    if (origin == QLatin1String("created"))
        return QObject::tr("新建");
    if (origin == QLatin1String("folder"))
        return QObject::tr("文件夹");
    return QObject::tr("ZIP");
}

QString updatedLabel(const QString &iso)
{
    const QDateTime time = QDateTime::fromString(iso, Qt::ISODate);
    if (!time.isValid())
        return {};
    return time.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

} // namespace

OpenProjectDialog::OpenProjectDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("打开项目"));
    resize(820, 520);
    setMinimumSize(680, 420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("打开项目"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    layout->addWidget(title);

    auto *hint = new QLabel(
        tr("列出 data/projects 下已创建或导入的项目。也可以浏览任意包含 project.json "
           "或 pack.mcmeta 的文件夹。"));
    hint->setObjectName(QStringLiteral("PanelHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(tr("搜索项目名称 / 版本…"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({tr("项目名称"), tr("来源"), tr("版本"), tr("最近更新"),
                             tr("目录")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setSortingEnabled(true);
    m_tree->sortByColumn(3, Qt::DescendingOrder);
    m_tree->header()->setStretchLastSection(true);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree, 1);

    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("PanelHint"));
    layout->addWidget(m_status);

    auto *buttonRow = new QHBoxLayout;
    auto *browseButton = new QPushButton(tr("浏览文件夹…"));
    browseButton->setObjectName(QStringLiteral("GhostButton"));
    buttonRow->addWidget(browseButton);
    buttonRow->addStretch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_openButton = buttons->addButton(tr("打开"), QDialogButtonBox::AcceptRole);
    m_openButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_openButton->setEnabled(false);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    buttonRow->addWidget(buttons);
    layout->addLayout(buttonRow);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_openButton, &QPushButton::clicked, this, [this] {
        if (!m_projectDir.isEmpty())
            accept();
    });
    connect(browseButton, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(
            this, tr("选择项目或资源包文件夹"), AppPaths::projectsRoot());
        if (folder.isEmpty())
            return;
        m_browsedFolder = folder;
        accept();
    });
    connect(m_search, &QLineEdit::textChanged, this,
            [this](const QString &text) { populate(text); });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &OpenProjectDialog::updateSelection);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                if (!item)
                    return;
                m_projectDir = item->data(0, Qt::UserRole).toString();
                if (!m_projectDir.isEmpty())
                    accept();
            });

    populate();
}

void OpenProjectDialog::populate(const QString &filter)
{
    const QVector<ProjectSummary> projects =
        ProjectService::listProjects(AppPaths::projectsRoot());
    m_tree->clear();
    const QString text = filter.trimmed();

    int visible = 0;
    for (const ProjectSummary &summary : projects) {
        const QString version = packFormatLabel(summary.packFormat);
        const QString haystack = QStringLiteral("%1 %2 %3 %4")
                                     .arg(summary.name, version,
                                          originLabel(summary.origin), summary.projectDir);
        if (!text.isEmpty() && !haystack.contains(text, Qt::CaseInsensitive))
            continue;

        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, summary.name);
        item->setText(1, originLabel(summary.origin));
        item->setText(2, version);
        item->setText(3, updatedLabel(summary.updatedAt));
        item->setText(4, QDir::toNativeSeparators(summary.projectDir));
        item->setData(0, Qt::UserRole, summary.projectDir);
        item->setToolTip(4, QDir::toNativeSeparators(summary.projectDir));
        if (!summary.originPath.isEmpty())
            item->setToolTip(1, QDir::toNativeSeparators(summary.originPath));
        ++visible;
    }

    m_status->setText(visible == 0 ? tr("没有找到项目")
                                   : tr("%1 个项目").arg(visible));
    m_projectDir.clear();
    m_openButton->setEnabled(false);
}

void OpenProjectDialog::updateSelection()
{
    const QList<QTreeWidgetItem *> selected = m_tree->selectedItems();
    m_projectDir = selected.isEmpty()
                       ? QString()
                       : selected.first()->data(0, Qt::UserRole).toString();
    m_openButton->setEnabled(!m_projectDir.isEmpty());
}

} // namespace mcstudio
