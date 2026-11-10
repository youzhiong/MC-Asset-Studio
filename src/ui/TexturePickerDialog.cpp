#include "ui/TexturePickerDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace mcstudio {

TexturePickerDialog::TexturePickerDialog(const QStringList &paths,
                                         const QString &title, QWidget *parent)
    : QDialog(parent)
    , m_paths(paths)
{
    setWindowTitle(title);
    resize(680, 520);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("搜索路径，例如 armor 或 item/ingot"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);

    m_count = new QLabel(this);
    m_count->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_count);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    m_list->setUniformItemSizes(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    for (const QString &path : m_paths)
        m_list->addItem(path);
    layout->addWidget(m_list, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    m_acceptButton = buttons->button(QDialogButtonBox::Ok);
    m_acceptButton->setText(tr("用作参考"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, &TexturePickerDialog::applyFilter);
    connect(m_list, &QListWidget::itemSelectionChanged, this,
            &TexturePickerDialog::updateSelection);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        if (m_list->currentItem())
            accept();
    });

    applyFilter(QString());
    m_acceptButton->setEnabled(false);
    m_search->setFocus();
}

QString TexturePickerDialog::selectedPath() const
{
    QListWidgetItem *item = m_list ? m_list->currentItem() : nullptr;
    return item ? item->text() : QString();
}

void TexturePickerDialog::applyFilter(const QString &text)
{
    const QString needle = text.trimmed();
    int visible = 0;
    for (int index = 0; index < m_list->count(); ++index) {
        QListWidgetItem *item = m_list->item(index);
        const bool match =
            needle.isEmpty() || item->text().contains(needle, Qt::CaseInsensitive);
        item->setHidden(!match);
        if (match)
            ++visible;
    }
    m_count->setText(tr("%1 个材质，当前显示 %2 个").arg(m_list->count()).arg(visible));
    updateSelection();
}

void TexturePickerDialog::updateSelection()
{
    if (m_acceptButton)
        m_acceptButton->setEnabled(m_list->currentItem() != nullptr);
}

} // namespace mcstudio
