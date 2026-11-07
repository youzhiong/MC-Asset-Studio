#include "ui/PackEditorPage.h"

#include "core/AssetNamespace.h"
#include "core/ModInfo.h"
#include "core/ModScanner.h"
#include "ui/CoverEditorDialog.h"

#include <QApplication>
#include <QBuffer>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>
#include <QAbstractItemView>

#include <algorithm>

namespace mcstudio {
namespace {

QString instanceDisplay(const InstanceInfo &instance)
{
    return QStringLiteral("%1 · %2").arg(instance.name, instance.loaderLabel());
}

} // namespace

PackEditorPage::PackEditorPage(const Project &project,
                               const GameEnvironmentSnapshot &environment,
                               const QString &referenceInstanceKey, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_environment(environment)
    , m_referenceKey(referenceInstanceKey)
{
    const QStringList existing = existingNamespaces();
    for (const QString &ns : existing)
        m_supported.insert(ns);
    for (const QString &ns : m_project.supportedNamespaces)
        m_supported.insert(ns);
    m_initialSupported = m_supported;

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto *title = new QLabel(tr("材质包编辑"));
    title->setObjectName(QStringLiteral("EmptyTitle"));
    layout->addWidget(title);

    auto *hint = new QLabel(
        tr("在这里管理资源包支持的模组和封面。修改先保留在页面上，点击底部"
           "“保存修改”后写入 pack/ 覆盖层。"));
    hint->setObjectName(QStringLiteral("EmptyText"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *metaRow = new QHBoxLayout;
    auto *metaLabel = new QLabel(
        tr("pack.mcmeta 的版本、描述和支持版本在“项目概览”里编辑。"));
    metaLabel->setObjectName(QStringLiteral("PanelHint"));
    metaLabel->setWordWrap(true);
    auto *overviewButton = new QPushButton(tr("打开项目概览"));
    overviewButton->setObjectName(QStringLiteral("GhostButton"));
    metaRow->addWidget(metaLabel, 1);
    metaRow->addWidget(overviewButton);
    layout->addLayout(metaRow);

    // Cover ---------------------------------------------------------------
    auto *coverCard = new QFrame;
    coverCard->setObjectName(QStringLiteral("ProjectCard"));
    coverCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *coverLayout = new QHBoxLayout(coverCard);
    coverLayout->setContentsMargins(18, 14, 18, 14);
    coverLayout->setSpacing(18);

    m_coverPreview = new QLabel;
    m_coverPreview->setFixedSize(180, 180);
    m_coverPreview->setAlignment(Qt::AlignCenter);
    m_coverPreview->setObjectName(QStringLiteral("CoverPreview"));
    coverLayout->addWidget(m_coverPreview);

    auto *coverInfo = new QVBoxLayout;
    coverInfo->setSpacing(8);
    auto *coverTitle = new QLabel(tr("封面 pack.png"));
    coverTitle->setObjectName(QStringLiteral("SectionTitle"));
    coverInfo->addWidget(coverTitle);
    auto *coverHint = new QLabel(
        tr("导入图片后可以缩放、拖动、旋转并裁剪为正方形。"
           "输出默认 512×512，可在编辑窗口里选择 64/128/256/512 或自定义。"));
    coverHint->setObjectName(QStringLiteral("PanelHint"));
    coverHint->setWordWrap(true);
    coverInfo->addWidget(coverHint);
    auto *importButton = new QPushButton(tr("导入/编辑封面…"));
    importButton->setObjectName(QStringLiteral("GhostButton"));
    importButton->setFixedWidth(150);
    coverInfo->addWidget(importButton);
    coverInfo->addStretch();
    coverLayout->addLayout(coverInfo, 1);
    layout->addWidget(coverCard);

    // Mods ----------------------------------------------------------------
    auto *modsHeader = new QHBoxLayout;
    auto *modsTitle = new QLabel(tr("支持的模组"));
    modsTitle->setObjectName(QStringLiteral("SectionTitle"));
    modsHeader->addWidget(modsTitle);
    modsHeader->addStretch();

    m_scopeCombo = new QComboBox;
    const InstanceInfo *reference = referenceInstance();
    m_scopeCombo->addItem(reference
                              ? tr("当前实例（%1）").arg(reference->name)
                              : tr("当前实例（未选择参考实例）"),
                          QStringLiteral("current"));
    m_scopeCombo->addItem(tr("所有扫描到的实例"), QStringLiteral("all"));
    modsHeader->addWidget(m_scopeCombo);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(tr("查询模组 / 命名空间…"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(240);
    modsHeader->addWidget(m_searchEdit);
    layout->addLayout(modsHeader);

    auto *modsHint = new QLabel(
        tr("打勾会在 pack/assets/<命名空间>/ 下创建文件夹；已有文件的命名空间"
           "不能取消。取消空文件夹不会删除任何文件。"));
    modsHint->setObjectName(QStringLiteral("PanelHint"));
    modsHint->setWordWrap(true);
    layout->addWidget(modsHint);

    m_modTree = new QTreeWidget;
    m_modTree->setColumnCount(5);
    m_modTree->setHeaderLabels({tr("支持"), tr("模组"), tr("ID"), tr("命名空间"),
                                tr("来源实例")});
    m_modTree->setRootIsDecorated(false);
    m_modTree->setUniformRowHeights(true);
    m_modTree->setAlternatingRowColors(true);
    m_modTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_modTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_modTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_modTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_modTree->header()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_modTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    layout->addWidget(m_modTree, 1);

    auto *footer = new QHBoxLayout;
    m_status = new QLabel(tr("选择要支持的模组，然后点击保存修改。"));
    m_status->setObjectName(QStringLiteral("PanelHint"));
    footer->addWidget(m_status, 1);
    m_saveButton = new QPushButton(tr("保存修改"));
    m_saveButton->setObjectName(QStringLiteral("PrimaryButton"));
    footer->addWidget(m_saveButton);
    layout->addLayout(footer);

    connect(overviewButton, &QPushButton::clicked, this, [this] {
        if (onOpenOverview)
            onOpenOverview();
    });
    connect(importButton, &QPushButton::clicked, this, &PackEditorPage::importCover);
    connect(m_saveButton, &QPushButton::clicked, this, &PackEditorPage::saveChanges);
    connect(m_scopeCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { rebuildMods(); });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i = 0; i < m_modTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = m_modTree->topLevelItem(i);
            const QString haystack = QStringLiteral("%1 %2 %3 %4")
                                         .arg(item->text(1), item->text(2),
                                              item->text(3), item->text(4));
            item->setHidden(!text.trimmed().isEmpty()
                            && !haystack.contains(text, Qt::CaseInsensitive));
        }
    });
    connect(m_modTree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem *item, int) { updateSupportedFromItem(item); });

    updateCoverPreview();
    rebuildMods();
}

const InstanceInfo *PackEditorPage::referenceInstance() const
{
    if (m_referenceKey.isEmpty())
        return nullptr;
    for (const GameRootInfo &root : m_environment.roots) {
        for (const InstanceInfo &instance : root.instances) {
            if (instance.key == m_referenceKey)
                return &instance;
        }
    }
    return nullptr;
}

QStringList PackEditorPage::existingNamespaces() const
{
    return namespacesFromPaths(m_project.files());
}

void PackEditorPage::rebuildMods()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_rows.clear();

    QVector<const InstanceInfo *> instances;
    if (m_scopeCombo->currentData().toString() == QLatin1String("all")) {
        for (const GameRootInfo &root : m_environment.roots) {
            for (const InstanceInfo &instance : root.instances)
                instances.append(&instance);
        }
    } else if (const InstanceInfo *reference = referenceInstance()) {
        instances.append(reference);
    }

    int totalJars = 0;
    for (const InstanceInfo *instance : instances) {
        for (const ModFileInfo &mod : instance->mods) {
            if (!mod.disabled)
                ++totalJars;
        }
    }

    QProgressDialog progress(tr("正在读取模组列表…"), QString(), 0,
                             qMax(1, totalJars), this);
    progress.setWindowTitle(tr("读取模组"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    progress.setAutoClose(false);
    progress.setCancelButton(nullptr);
    if (totalJars > 450) {
        progress.setValue(0);
        QCoreApplication::processEvents();
    }

    QSet<QString> existing;
    for (const QString &ns : existingNamespaces())
        existing.insert(ns);
    QHash<QString, int> rowIndex;
    int scannedJars = 0;

    const auto addRow = [this, &rowIndex](const QString &name, const QString &modId,
                                          const QStringList &namespaces,
                                          const QString &instance) {
        QStringList cleaned;
        for (const QString &ns : namespaces) {
            if (!ns.isEmpty() && !cleaned.contains(ns))
                cleaned.append(ns);
        }
        if (cleaned.isEmpty()) {
            if (!modId.isEmpty())
                cleaned.append(modId);
            else
                return;
        }
        QString key = modId.toLower();
        if (key.isEmpty())
            key = name.toLower() + QLatin1Char('|') + cleaned.join(QLatin1Char(','));
        if (rowIndex.contains(key)) {
            ModRow &existingRow = m_rows[rowIndex.value(key)];
            for (const QString &ns : cleaned) {
                if (!existingRow.namespaces.contains(ns))
                    existingRow.namespaces.append(ns);
            }
            if (!existingRow.instance.contains(instance))
                existingRow.instance += QStringLiteral(" / ") + instance;
            return;
        }
        ModRow row;
        row.name = name;
        row.modId = modId;
        row.namespaces = cleaned;
        row.instance = instance;
        rowIndex.insert(key, m_rows.size());
        m_rows.append(row);
    };

    for (const InstanceInfo *instance : instances) {
        for (const ModFileInfo &mod : instance->mods) {
            if (mod.disabled)
                continue;
            const JarModScan scan =
                ModScanner::scanJar(mod, instance->key, instance->loader);
            ++scannedJars;
            if (totalJars > 450 && scannedJars % 5 == 0) {
                progress.setValue(scannedJars);
                progress.setLabelText(tr("正在读取 %1 · %2/%3")
                                          .arg(instance->name)
                                          .arg(scannedJars)
                                          .arg(totalJars));
                QCoreApplication::processEvents();
            }
            if (!scan.ok())
                continue;

            if (scan.mods.isEmpty()) {
                if (!scan.namespaces.isEmpty()) {
                    addRow(mod.fileName, QString(), scan.namespaces,
                           instanceDisplay(*instance));
                }
                continue;
            }

            for (const ModInfo &info : scan.mods) {
                QStringList namespaces = info.namespaces;
                if (namespaces.isEmpty())
                    namespaces = scan.namespaces;
                if (namespaces.isEmpty() && !info.modId.isEmpty())
                    namespaces.append(info.modId);
                addRow(info.displayName.isEmpty() ? info.modId : info.displayName,
                       info.modId, namespaces, instanceDisplay(*instance));
            }
        }
    }

    QSet<QString> covered;
    for (ModRow &row : m_rows) {
        for (const QString &ns : row.namespaces)
            covered.insert(ns);
        for (const QString &ns : row.namespaces) {
            if (existing.contains(ns))
                row.existing = true;
        }
    }
    for (const QString &ns : existing) {
        if (covered.contains(ns))
            continue;
        ModRow row;
        row.name = tr("已有命名空间");
        row.modId = ns;
        row.namespaces = {ns};
        row.instance = tr("资源包");
        row.existing = true;
        m_rows.append(row);
    }

    for (const QString &ns : m_supported) {
        if (!covered.contains(ns)) {
            // Keep previously selected namespaces from other scopes visible.
            bool found = false;
            for (const ModRow &row : m_rows) {
                if (row.namespaces.contains(ns)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                ModRow row;
                row.name = tr("已选择（当前范围未找到模组）");
                row.modId = ns;
                row.namespaces = {ns};
                row.instance = tr("项目记录");
                m_rows.append(row);
            }
        }
    }

    std::sort(m_rows.begin(), m_rows.end(), [this](const ModRow &a, const ModRow &b) {
        const auto selected = [this](const ModRow &row) {
            for (const QString &ns : row.namespaces) {
                if (m_supported.contains(ns))
                    return true;
            }
            return false;
        };
        const bool aSelected = selected(a);
        const bool bSelected = selected(b);
        if (aSelected != bSelected)
            return aSelected;
        return a.name.localeAwareCompare(b.name) < 0;
    });

    rebuildTable();
    progress.close();
    QApplication::restoreOverrideCursor();
}

void PackEditorPage::rebuildTable()
{
    QSignalBlocker blocker(m_modTree);
    m_modTree->clear();
    for (int i = 0; i < m_rows.size(); ++i) {
        const ModRow &row = m_rows.at(i);
        auto *item = new QTreeWidgetItem(m_modTree);
        item->setText(0, QString());
        item->setText(1, row.name);
        item->setText(2, row.modId);
        item->setText(3, row.namespaces.join(QStringLiteral(", ")));
        item->setText(4, row.instance);
        item->setData(0, Qt::UserRole, i);

        bool checked = row.existing;
        for (const QString &ns : row.namespaces) {
            if (m_supported.contains(ns)) {
                checked = true;
                break;
            }
        }
        item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
        if (row.existing) {
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            item->setToolTip(0, tr("命名空间已有文件，不能在此取消"));
        } else {
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                           | Qt::ItemIsUserCheckable);
        }
        item->setToolTip(3, row.namespaces.join(QStringLiteral(", ")));
    }
    QStringList selected = m_supported.values();
    selected.sort(Qt::CaseInsensitive);
    const QString summary = selected.size() > 8
                                ? selected.mid(0, 8).join(QStringLiteral(", "))
                                      + QStringLiteral(" …")
                                : selected.join(QStringLiteral(", "));
    m_status->setText(selected.isEmpty()
                          ? tr("尚未选择模组。")
                          : tr("已选择 %1 个命名空间：%2")
                                .arg(selected.size())
                                .arg(summary));
    m_modTree->scrollToTop();
}

bool PackEditorPage::isDirty() const
{
    return m_coverChanged || m_supported != m_initialSupported;
}

void PackEditorPage::setNamespaceSupported(const QString &namespaceId, bool supported)
{
    if (namespaceId.isEmpty())
        return;
    if (supported)
        m_supported.insert(namespaceId);
    else
        m_supported.remove(namespaceId);
    rebuildTable();
}

void PackEditorPage::saveNow()
{
    saveChanges();
}

void PackEditorPage::setScopeAllForTesting(bool all)
{
    m_scopeCombo->setCurrentIndex(all ? 1 : 0);
}

void PackEditorPage::setCoverForTesting(const QImage &cover)
{
    if (cover.isNull())
        return;
    const int side = qMin(cover.width(), cover.height());
    const QRect crop((cover.width() - side) / 2, (cover.height() - side) / 2, side, side);
    m_pendingCover = cover.copy(crop).scaled(512, 512, Qt::IgnoreAspectRatio,
                                             Qt::SmoothTransformation);
    m_coverChanged = true;
    updateCoverPreview();
    saveChanges();
}

void PackEditorPage::updateSupportedFromItem(QTreeWidgetItem *item){
    if (!item)
        return;
    const int rowIndex = item->data(0, Qt::UserRole).toInt();
    if (rowIndex < 0 || rowIndex >= m_rows.size())
        return;
    const ModRow &row = m_rows.at(rowIndex);
    if (row.existing)
        return;

    const bool checked = item->checkState(0) == Qt::Checked;
    for (const QString &ns : row.namespaces) {
        if (checked)
            m_supported.insert(ns);
        else
            m_supported.remove(ns);
    }
    QStringList selected = m_supported.values();
    selected.sort(Qt::CaseInsensitive);
    m_status->setText(tr("已选择 %1 个命名空间：%2，点击“保存修改”生效。")
                          .arg(selected.size())
                          .arg(selected.join(QStringLiteral(", "))));
}

void PackEditorPage::updateCoverPreview()
{
    QImage image = m_pendingCover;
    if (image.isNull()) {
        const QByteArray bytes = m_project.readBytes(QStringLiteral("pack.png"));
        if (!bytes.isEmpty())
            image = QImage::fromData(bytes);
    }
    if (image.isNull()) {
        m_coverPreview->setPixmap(QPixmap());
        m_coverPreview->setText(tr("无封面"));
        return;
    }
    m_coverPreview->setText(QString());
    m_coverPreview->setPixmap(QPixmap::fromImage(image).scaled(
        180, 180, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void PackEditorPage::importCover()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择资源包封面"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty())
        return;

    QImage image(path);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("无法读取图片"), tr("无法读取：%1").arg(path));
        return;
    }

    if (image.width() != image.height()) {
        const auto answer = QMessageBox::question(
            this, tr("图片不是正方形"),
            tr("这张图片是 %1×%2，不是 1:1。\n是否裁剪为正方形封面？\n\n"
               "选择“否”会放弃本次导入。")
                .arg(image.width())
                .arg(image.height()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes)
            return;
    }

    CoverEditorDialog dialog(image, 512, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_pendingCover = dialog.resultImage();
    m_coverChanged = true;
    updateCoverPreview();
    m_status->setText(tr("封面已修改，点击“保存修改”写入 pack/pack.png。"));
}

void PackEditorPage::saveChanges()
{
    QStringList supported = m_supported.values();
    supported.sort(Qt::CaseInsensitive);

    QStringList keepBecauseNotEmpty;
    for (const QString &ns : m_initialSupported) {
        if (m_supported.contains(ns))
            continue;
        const QString dir = QDir(m_project.packDir)
                                .filePath(QStringLiteral("assets/%1").arg(ns));
        if (!QFileInfo::exists(dir))
            continue;
        const QStringList entries =
            QDir(dir).entryList(QDir::AllEntries | QDir::NoDotAndDotDot);
        if (entries.isEmpty())
            QDir(dir).removeRecursively();
        else
            keepBecauseNotEmpty.append(ns);
    }
    for (const QString &ns : keepBecauseNotEmpty) {
        if (!supported.contains(ns))
            supported.append(ns);
    }
    supported.sort(Qt::CaseInsensitive);

    for (const QString &ns : supported) {
        const QString dir = QDir(m_project.packDir)
                                .filePath(QStringLiteral("assets/%1").arg(ns));
        if (!QDir().mkpath(dir)) {
            QMessageBox::critical(this, tr("保存失败"),
                                  tr("无法创建目录：%1").arg(dir));
            return;
        }
    }
    QDir().mkpath(QDir(m_project.packDir).filePath(QStringLiteral("assets")));

    if (m_coverChanged && !m_pendingCover.isNull()) {
        const QString coverPath =
            QDir(m_project.packDir).filePath(QStringLiteral("pack.png"));
        if (!QDir().mkpath(QFileInfo(coverPath).absolutePath())) {
            QMessageBox::critical(this, tr("保存失败"), tr("无法创建 pack/ 目录。"));
            return;
        }
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        if (!m_pendingCover.save(&buffer, "PNG")) {
            QMessageBox::critical(this, tr("保存失败"), tr("封面编码失败。"));
            return;
        }
        QSaveFile file(coverPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(png) < 0 || !file.commit()) {
            QMessageBox::critical(this, tr("保存失败"),
                                  tr("无法写入 pack.png：%1").arg(file.errorString()));
            return;
        }
        m_coverChanged = false;
    }

    m_project.supportedNamespaces = supported;
    m_project.updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QString error;
    if (!m_project.save(&error)) {
        QMessageBox::critical(this, tr("保存失败"), error);
        return;
    }
    m_project.scan();
    m_initialSupported.clear();
    for (const QString &ns : supported)
        m_initialSupported.insert(ns);

    m_status->setText(tr("已保存：%1 个命名空间%2")
                          .arg(supported.size())
                          .arg(keepBecauseNotEmpty.isEmpty()
                                   ? QString()
                                   : tr("（%1 个非空文件夹保留）")
                                         .arg(keepBecauseNotEmpty.size())));
    if (onSaved)
        onSaved();
}

} // namespace mcstudio
