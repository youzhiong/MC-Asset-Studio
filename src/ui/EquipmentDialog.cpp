#include "ui/EquipmentDialog.h"

#include "core/AppPaths.h"
#include "core/ZipArchive.h"
#include "ui/TintDialog.h"

#include <QBuffer>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace mcstudio {
namespace {

QString namespaceOf(const QString &path)
{
    const QString normalized = path;
    if (!normalized.startsWith(QLatin1String("assets/")))
        return {};
    const QStringList parts = normalized.split(QLatin1Char('/'));
    return parts.size() >= 2 ? parts.at(1) : QString();
}

} // namespace

EquipmentDialog::EquipmentDialog(const Project &project, const QString &zipPath,
                                 const QVector<TemplateEntry> &entries,
                                 QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_zipPath(zipPath)
    , m_entries(entries)
{
    setWindowTitle(tr("生成整套装备"));
    resize(680, 620);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(9);

    auto *hint = new QLabel(
        tr("这个模板包里有多张 PNG。生成时会把它们按 assets/<命名空间>/… 的路径"
           "写入当前项目的 pack/ 覆盖层。一次写多个文件，不能整体撤销；"
           "不想保留时请先备份或在导出前检查。"),
        this);
    hint->setObjectName(QStringLiteral("PanelHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_list = new QListWidget(this);
    QString oldNamespace;
    for (const TemplateEntry &entry : m_entries) {
        auto *item = new QListWidgetItem(entry.entryPath, m_list);
        item->setToolTip(entry.entryPath);
        if (oldNamespace.isEmpty())
            oldNamespace = namespaceOf(entry.entryPath);
        if (entry.entryPath.contains(QStringLiteral("/models/armor/")))
            item->setText(entry.entryPath + tr("  ·  盔甲层"));
    }
    layout->addWidget(m_list, 1);

    auto *namespaceRow = new QHBoxLayout;
    auto *namespaceTitle = new QLabel(tr("目标命名空间"), this);
    namespaceTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_namespaceEdit = new QLineEdit(this);
    m_namespaceEdit->setText(oldNamespace.isEmpty() ? QStringLiteral("minecraft")
                                                    : oldNamespace);
    m_namespaceEdit->setToolTip(
        tr("把 assets/旧命名空间/ 替换成 assets/新命名空间/；例如改成你的模组 ID"));
    namespaceRow->addWidget(namespaceTitle);
    namespaceRow->addWidget(m_namespaceEdit, 1);
    layout->addLayout(namespaceRow);

    m_overwriteCheck = new QCheckBox(tr("覆盖 pack/ 里已经存在的同名文件"), this);
    layout->addWidget(m_overwriteCheck);
    m_tintCheck = new QCheckBox(tr("应用调色（同一套配色套到所有文件）"), this);
    layout->addWidget(m_tintCheck);

    auto *recipeRow = new QHBoxLayout;
    auto *recipeButton = new QPushButton(tr("选择调色…"), this);
    recipeButton->setObjectName(QStringLiteral("GhostButton"));
    connect(recipeButton, &QPushButton::clicked, this,
            &EquipmentDialog::chooseRecipe);
    recipeRow->addWidget(recipeButton);
    recipeRow->addStretch(1);
    layout->addLayout(recipeRow);

    m_info = new QLabel(tr("共 %1 个文件。").arg(m_entries.size()), this);
    m_info->setObjectName(QStringLiteral("Muted"));
    m_info->setWordWrap(true);
    layout->addWidget(m_info);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("生成整套"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &EquipmentDialog::generate);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString EquipmentDialog::targetPath(const TemplateEntry &entry) const
{
    QString path = entry.entryPath;
    if (!path.startsWith(QLatin1String("assets/")))
        return path;
    QStringList parts = path.split(QLatin1Char('/'));
    if (parts.size() < 3)
        return path;
    parts[1] = m_namespaceEdit->text().trimmed();
    return parts.join(QLatin1Char('/'));
}

void EquipmentDialog::chooseRecipe()
{
    if (m_entries.isEmpty())
        return;
    ZipArchive archive(m_zipPath);
    const QImage image = QImage::fromData(archive.read(m_entries.first().entryPath));
    if (image.isNull()) {
        QMessageBox::warning(this, tr("选择调色"), tr("无法读取模板图片。"));
        return;
    }
    TintDialog dialog(image, false, false, false, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_recipe = dialog.recipe();
    m_hasRecipe = true;
    m_tintCheck->setChecked(true);
    m_info->setText(tr("已选择调色配方；生成时会套用到全部 %1 个文件。")
                        .arg(m_entries.size()));
}

void EquipmentDialog::generate()
{
    const QString newNamespace = m_namespaceEdit->text().trimmed();
    if (newNamespace.isEmpty()
        || !QRegularExpression(QStringLiteral("^[A-Za-z0-9_.-]+$"))
                .match(newNamespace)
                .hasMatch()) {
        QMessageBox::warning(this, tr("命名空间无效"),
                             tr("只能使用字母、数字、下划线、点和短横线。"));
        return;
    }

    ZipArchive archive(m_zipPath);
    if (!archive.isOpen()) {
        QMessageBox::warning(this, tr("无法读取模板包"), m_zipPath);
        return;
    }

    const bool applyTint = m_tintCheck->isChecked() && m_hasRecipe;
    const bool overwrite = m_overwriteCheck->isChecked();
    int written = 0;
    int skipped = 0;
    QStringList generated;
    QStringList errors;

    for (const TemplateEntry &entry : m_entries) {
        const QString path = targetPath(entry);
        if (m_project.exists(path) && !overwrite) {
            ++skipped;
            continue;
        }
        QImage image = QImage::fromData(archive.read(entry.entryPath));
        if (image.isNull()) {
            errors.append(tr("%1：无法读取").arg(entry.entryPath));
            continue;
        }
        if (applyTint)
            image = m_recipe.apply(image);

        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG")) {
            errors.append(tr("%1：无法编码 PNG").arg(path));
            continue;
        }
        QString error;
        if (!m_project.writeOutputFile(path, bytes, &error)) {
            errors.append(tr("%1：%2").arg(path, error));
            continue;
        }
        generated.append(path);
        ++written;
    }

    QString message = tr("已生成 %1 个文件，跳过 %2 个已存在的文件。")
                          .arg(written)
                          .arg(skipped);
    if (!errors.isEmpty()) {
        message += tr("\n\n有 %1 个失败：\n%2")
                       .arg(errors.size())
                       .arg(errors.mid(0, 5).join(QLatin1Char('\n')));
    }
    QMessageBox::information(this, tr("生成整套装备"), message);

    if (!generated.isEmpty() && onGenerated)
        onGenerated(generated);
    accept();
}

} // namespace mcstudio
