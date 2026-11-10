#include "ui/TemplateDialog.h"

#include "ui/TexturePickerDialog.h"
#include "ui/Theme.h"

#include <QAbstractButton>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSet>
#include <QUrl>
#include <QVBoxLayout>

namespace mcstudio {
namespace {

QImage toGrayscale(const QImage &source)
{
    if (source.isNull())
        return {};
    QImage image = source.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = row[x];
            const int alpha = qAlpha(pixel);
            if (alpha == 0)
                continue;
            const int luminance = (54 * qRed(pixel) + 183 * qGreen(pixel)
                                   + 19 * qBlue(pixel))
                                  >> 8;
            row[x] = qRgba(luminance, luminance, luminance, alpha);
        }
    }
    return image;
}

struct VanillaTemplate {
    const char *path;
    const char *name;
};

const VanillaTemplate kVanillaTemplates[] = {
    {"assets/minecraft/textures/item/iron_ingot.png", "原版-铁锭"},
    {"assets/minecraft/textures/block/iron_block.png", "原版-铁块"},
    {"assets/minecraft/textures/item/iron_helmet.png", "原版-铁头盔"},
    {"assets/minecraft/textures/item/iron_chestplate.png", "原版-铁胸甲"},
    {"assets/minecraft/textures/item/iron_leggings.png", "原版-铁护腿"},
    {"assets/minecraft/textures/item/iron_boots.png", "原版-铁靴子"},
    {"assets/minecraft/textures/item/iron_axe.png", "原版-铁斧"},
    {"assets/minecraft/textures/item/iron_pickaxe.png", "原版-铁镐"},
    {"assets/minecraft/textures/item/iron_shovel.png", "原版-铁锹"},
    {"assets/minecraft/textures/item/iron_sword.png", "原版-铁剑"},
    {"assets/minecraft/textures/item/iron_hoe.png", "原版-铁锄"},
    {"assets/minecraft/textures/item/redstone.png", "原版-红石粉"},
    {"assets/minecraft/textures/item/diamond.png", "原版-钻石"},
    {"assets/minecraft/textures/item/emerald.png", "原版-绿宝石"},
    {"assets/minecraft/textures/item/lapis_lazuli.png", "原版-青金石"},
    {"assets/minecraft/textures/item/amethyst_shard.png", "原版-紫水晶"},
    {"assets/minecraft/textures/item/quartz.png", "原版-下界石英"},
    {"assets/minecraft/textures/item/raw_iron.png", "原版-粗铁"},
    {"assets/minecraft/textures/item/raw_copper.png", "原版-粗铜"},
    {"assets/minecraft/textures/item/raw_gold.png", "原版-粗金"},
    {"assets/minecraft/textures/item/gold_ingot.png", "原版-金锭"},
    {"assets/minecraft/textures/item/copper_ingot.png", "原版-铜锭"},
    {"assets/minecraft/textures/item/netherite_ingot.png", "原版-下界合金锭"},
    {"assets/minecraft/textures/block/gold_block.png", "原版-金块"},
    {"assets/minecraft/textures/block/diamond_block.png", "原版-钻石块"},
    {"assets/minecraft/textures/models/armor/iron_layer_1.png", "原版-铁盔甲层1"},
    {"assets/minecraft/textures/models/armor/iron_layer_2.png", "原版-铁盔甲层2"},
};

} // namespace

QImage checkerPreview(const QImage &image, int size)
{
    const Theme::Colors colors = Theme::colors();
    QImage canvas(size, size, QImage::Format_ARGB32);
    canvas.fill(colors.checkerA);
    constexpr int cell = 8;
    QPainter painter(&canvas);
    for (int y = 0; y < size; y += cell) {
        for (int x = 0; x < size; x += cell) {
            if (((x / cell) + (y / cell)) % 2 == 0)
                continue;
            painter.fillRect(QRect(x, y, cell, cell), colors.checkerB);
        }
    }
    if (!image.isNull()) {
        const QImage scaled =
            image.scaled(size - 16, size - 16, Qt::KeepAspectRatio,
                         Qt::FastTransformation);
        painter.drawImage(QPoint((size - scaled.width()) / 2,
                                 (size - scaled.height()) / 2),
                          scaled);
    }
    painter.end();
    return canvas;
}

TemplateDialog::TemplateDialog(std::function<QStringList()> pathProvider,
                               std::function<QByteArray(const QString &)> bytesProvider,
                               QWidget *parent)
    : QDialog(parent)
    , m_pathProvider(std::move(pathProvider))
    , m_bytesProvider(std::move(bytesProvider))
{
    setWindowTitle(tr("模板库"));
    resize(820, 620);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(9);

    auto *hint = new QLabel(
        tr("模板就是一个 ZIP，里面放若干 PNG；也可以直接把 PNG 放进模板文件夹。"
           "内置的“锭/头盔…”只是占位图；在右侧“参考实例”选好游戏版本后，"
           "打开模板库会自动从原版 jar 读取铁锭、铁块、装备、工具、宝石、粗矿等，"
           "转成灰度模板“原版-…”。也可以点“从原版 jar 生成模板…”重新生成。"),
        this);
    hint->setObjectName(QStringLiteral("PanelHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *content = new QHBoxLayout;
    content->setSpacing(14);

    m_list = new QListWidget(this);
    m_list->setMinimumWidth(300);
    m_list->setAlternatingRowColors(true);
    content->addWidget(m_list, 1);

    auto *right = new QVBoxLayout;
    right->setSpacing(8);
    m_preview = new QLabel(this);
    m_preview->setFixedSize(200, 200);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setPixmap(QPixmap::fromImage(checkerPreview(QImage(), 200)));
    right->addWidget(m_preview, 0, Qt::AlignHCenter);

    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("Muted"));
    m_info->setWordWrap(true);
    right->addWidget(m_info);

    m_useButton = new QPushButton(tr("使用此模板"), this);
    m_useButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_useButton->setEnabled(false);
    connect(m_useButton, &QPushButton::clicked, this, [this] {
        if (!m_selectedImage.isNull())
            accept();
    });
    right->addWidget(m_useButton);

    m_equipmentButton = new QPushButton(tr("生成整套…"), this);
    m_equipmentButton->setObjectName(QStringLiteral("GhostButton"));
    m_equipmentButton->setEnabled(false);
    m_equipmentButton->setToolTip(
        tr("这个模板包里有多个 assets/ PNG 时，可以按路径一次生成多个文件"));
    connect(m_equipmentButton, &QPushButton::clicked, this, [this] {
        if (m_selectedPackZip.isEmpty())
            return;
        m_equipmentRequested = true;
        accept();
    });
    right->addWidget(m_equipmentButton);

    auto *importRow = new QHBoxLayout;
    auto *importZipButton = new QPushButton(tr("导入模板 ZIP…"), this);
    importZipButton->setObjectName(QStringLiteral("GhostButton"));
    connect(importZipButton, &QPushButton::clicked, this, &TemplateDialog::importZip);
    auto *fromGameButton = new QPushButton(tr("从游戏/模组导入…"), this);
    fromGameButton->setObjectName(QStringLiteral("GhostButton"));
    connect(fromGameButton, &QPushButton::clicked, this,
            &TemplateDialog::importFromLibrary);
    importRow->addWidget(importZipButton);
    importRow->addWidget(fromGameButton);
    right->addLayout(importRow);

    auto *vanillaButton = new QPushButton(tr("从原版 jar 生成模板…"), this);
    vanillaButton->setObjectName(QStringLiteral("GhostButton"));
    vanillaButton->setToolTip(
        tr("按原版路径读取铁锭/铁块/装备/工具/宝石/粗矿等，转成灰度后放进模板库；"
           "不会把原版贴图打包进软件"));
    connect(vanillaButton, &QPushButton::clicked, this,
            &TemplateDialog::importVanillaSet);
    right->addWidget(vanillaButton);

    auto *folderButton = new QPushButton(tr("打开模板文件夹"), this);
    folderButton->setObjectName(QStringLiteral("GhostButton"));
    connect(folderButton, &QPushButton::clicked, this,
            &TemplateDialog::openTemplatesFolder);
    right->addWidget(folderButton);
    right->addStretch(1);
    content->addLayout(right);
    layout->addLayout(content, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int) { loadSelection(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        if (!m_selectedImage.isNull())
            accept();
    });

    refresh();
    if (!hasVanillaSet() && importVanillaTemplates(true) > 0) {
        refresh();
        for (int row = 0; row < m_list->count(); ++row) {
            const int index = m_list->item(row)->data(Qt::UserRole).toInt();
            if (m_entries.at(index).name.startsWith(QStringLiteral("原版-铁锭"))) {
                m_list->setCurrentRow(row);
                break;
            }
        }
    }
}

bool TemplateDialog::hasVanillaSet() const
{
    for (const TemplateEntry &entry : m_entries) {
        if (entry.name.startsWith(QStringLiteral("原版-")))
            return true;
    }
    return false;
}

int TemplateDialog::importVanillaTemplates(bool quiet)
{
    if (!m_pathProvider || !m_bytesProvider) {
        if (!quiet) {
            QMessageBox::information(
                this, tr("从原版生成模板"),
                tr("当前没有可读取的游戏资源。请先在主界面右侧“参考实例”"
                   "选择一个含原版 assets 的游戏版本。"));
        }
        return 0;
    }

    const QStringList paths = m_pathProvider();

    QSet<QString> existingNames;
    for (const TemplateEntry &entry : m_entries)
        existingNames.insert(entry.name);

    int imported = 0;
    int missing = 0;
    QString firstError;

    for (const VanillaTemplate &item : kVanillaTemplates) {
        const QString name = QString::fromUtf8(item.name);
        if (existingNames.contains(name))
            continue;
        const QString path = QString::fromLatin1(item.path);
        const QByteArray bytes = m_bytesProvider(path);
        if (bytes.isEmpty()) {
            ++missing;
            continue;
        }
        const QImage image = toGrayscale(QImage::fromData(bytes));
        if (image.isNull()) {
            ++missing;
            continue;
        }
        QString error;
        if (m_library.importImage(image, name, &error)) {
            existingNames.insert(name);
            ++imported;
        } else if (firstError.isEmpty()) {
            firstError = error;
        }
    }

    // A few mods add sapphire/ruby; pick them up if the reference stack has them.
    for (const QString &path : paths) {
        if (!path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
            continue;
        const QString lower = path.toLower();
        QString name;
        if (lower.contains(QStringLiteral("sapphire")))
            name = QStringLiteral("模组-蓝宝石");
        else if (lower.contains(QStringLiteral("ruby")))
            name = QStringLiteral("模组-红宝石");
        if (name.isEmpty() || existingNames.contains(name)
            || !lower.contains(QStringLiteral("/textures/"))
            || lower.contains(QStringLiteral("block/"))) {
            continue;
        }
        const QImage image = toGrayscale(QImage::fromData(m_bytesProvider(path)));
        if (image.isNull())
            continue;
        QString error;
        if (m_library.importImage(image, name, &error)) {
            existingNames.insert(name);
            ++imported;
        }
    }

    if (!quiet) {
        if (imported == 0) {
            QMessageBox::information(
                this, tr("从原版生成模板"),
                tr("没有读到原版材质。\n\n"
                   "请先在主界面右侧“参考实例”选择游戏版本；"
                   "如果列表里没有，请到“编辑 → 首选项 → 游戏目录”添加。\n\n"
                   "缺失 %1 项。%2")
                    .arg(missing)
                    .arg(firstError));
        } else {
            QMessageBox::information(
                this, tr("从原版生成模板"),
                tr("已生成 %1 个灰度模板（缺失 %2 项）。\n"
                   "它们现在出现在模板列表里，名称为“原版-…”。")
                    .arg(imported)
                    .arg(missing));
        }
    }
    return imported;
}

void TemplateDialog::importVanillaSet()
{
    const int imported = importVanillaTemplates(false);
    if (imported <= 0)
        return;
    refresh();
    for (int row = 0; row < m_list->count(); ++row) {
        const int index = m_list->item(row)->data(Qt::UserRole).toInt();
        if (m_entries.at(index).name.startsWith(QStringLiteral("原版-铁锭"))) {
            m_list->setCurrentRow(row);
            break;
        }
    }
}

void TemplateDialog::refresh()
{
    QString keep = m_selectedName;
    m_entries = m_library.entries();
    bool hasVanilla = false;
    for (const TemplateEntry &entry : m_entries) {
        if (entry.name.startsWith(QStringLiteral("原版-"))) {
            hasVanilla = true;
            break;
        }
    }

    m_list->clear();
    int selectedRow = -1;
    int row = 0;

    for (int index = 0; index < m_entries.size(); ++index) {
        const TemplateEntry &entry = m_entries.at(index);
        // Once the real vanilla-derived set exists, hide the hand-drawn
        // placeholders and the built-in equipment icon pack.
        if (hasVanilla && (entry.builtin
                           || entry.pack == QLatin1String("builtin-equipment"))) {
            continue;
        }
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  ·  %2").arg(entry.name, entry.pack), m_list);
        item->setData(Qt::UserRole, index);
        if (!keep.isEmpty() && entry.name == keep)
            selectedRow = row;
        ++row;
    }

    if (m_list->count() == 0) {
        m_info->setText(tr("还没有模板。导入一个 ZIP，或把 PNG 放进模板文件夹。"));
        return;
    }
    if (selectedRow < 0)
        selectedRow = 0;
    m_list->setCurrentRow(selectedRow);
}

void TemplateDialog::loadSelection()
{
    QListWidgetItem *current = m_list->currentItem();
    if (!current)
        return;
    const int index = current->data(Qt::UserRole).toInt();
    if (index < 0 || index >= m_entries.size())
        return;
    const TemplateEntry &entry = m_entries.at(index);
    m_selectedImage = m_library.load(entry);
    m_selectedName = entry.name;
    m_selectedPackZip = entry.zipPath;
    m_equipmentRequested = false;
    const bool multiAsset =
        !entry.zipPath.isEmpty()
        && m_library.packAssets(entry.zipPath).size() > 1;
    m_equipmentButton->setEnabled(multiAsset);
    m_preview->setPixmap(QPixmap::fromImage(checkerPreview(m_selectedImage, 200)));
    m_useButton->setEnabled(!m_selectedImage.isNull());
    m_info->setText(tr("%1\n%2 × %3 · 来自 %4%5")
                        .arg(entry.name)
                        .arg(m_selectedImage.width())
                        .arg(m_selectedImage.height())
                        .arg(entry.pack)
                        .arg(multiAsset ? tr("\n这个包可以生成整套") : QString()));
}

void TemplateDialog::importZip()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择模板包"), QString(), tr("ZIP (*.zip);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    QString error;
    if (!m_library.importZip(path, &error)) {
        QMessageBox::warning(this, tr("导入模板失败"), error);
        return;
    }
    refresh();
}

void TemplateDialog::importFromLibrary()
{
    if (!m_pathProvider)
        return;

    const QStringList paths = m_pathProvider();
    if (paths.isEmpty()) {
        QMessageBox::information(this, tr("从游戏/模组导入"),
                                 tr("当前项目或参考实例里没有可用的材质。"));
        return;
    }

    TexturePickerDialog picker(paths, tr("选择一张材质作为模板"), this);
    if (picker.exec() != QDialog::Accepted)
        return;
    const QString path = picker.selectedPath();
    if (path.isEmpty())
        return;

    QImage image;
    if (m_bytesProvider)
        image = QImage::fromData(m_bytesProvider(path));
    if (image.isNull()) {
        QMessageBox::warning(this, tr("无法读取模板"),
                             tr("这个材质不是可读取的图片。"));
        return;
    }

    QMessageBox question(this);
    question.setWindowTitle(tr("模板处理"));
    question.setIcon(QMessageBox::Question);
    question.setText(tr("要把这张材质转成灰度再保存为模板吗？\n\n"
                        "转成灰度后，用“调色”可以快速换成铁/金/钻石等颜色；"
                        "保留原色则适合直接当参考或继续修图。"));
    QPushButton *grayscale =
        question.addButton(tr("转成灰度"), QMessageBox::AcceptRole);
    QPushButton *keep =
        question.addButton(tr("保留原色"), QMessageBox::ActionRole);
    question.addButton(tr("取消"), QMessageBox::RejectRole);
    question.exec();
    QAbstractButton *clicked = question.clickedButton();
    if (!clicked || (clicked != grayscale && clicked != keep))
        return;

    const QImage stored = clicked == grayscale ? toGrayscale(image) : image;
    QString error;
    if (!m_library.importImage(stored, QFileInfo(path).completeBaseName(), &error)) {
        QMessageBox::warning(this, tr("保存模板失败"), error);
        return;
    }
    refresh();
}

void TemplateDialog::openTemplatesFolder()
{
    QString error;
    if (!TemplateLibrary::ensureRoot(&error)) {
        QMessageBox::warning(this, tr("模板文件夹"), error);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(TemplateLibrary::root()));
}

} // namespace mcstudio
