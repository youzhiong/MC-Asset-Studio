#include "ui/MergeColorsDialog.h"

#include "ui/Theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

namespace mcstudio {

MergeColorsDialog::MergeColorsDialog(
    const QVector<TextureDocument::ColorCount> &colors, bool animated,
    QWidget *parent)
    : QDialog(parent)
    , m_colors(colors)
    , m_animated(animated)
{
    setWindowTitle(tr("合并颜色"));
    resize(520, 620);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(9);

    auto *hint = new QLabel(
        tr("勾选两种或更多颜色，把它们合并成一种。适合把照片里几乎一样的颜色收成同一种。"
           "合并会记入撤销。"),
        this);
    hint->setObjectName(QStringLiteral("PanelHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    for (const auto &entry : m_colors) {
        const QColor color = QColor::fromRgba(entry.rgba);
        auto *item = new QListWidgetItem(
            tr("%1 · %2 像素")
                .arg(color.name(QColor::HexRgb).toUpper())
                .arg(entry.count),
            m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(entry.rgba));
        item->setIcon(swatchIcon(color));
    }
    layout->addWidget(m_list, 1);

    auto *targetRow = new QHBoxLayout;
    auto *targetTitle = new QLabel(tr("合并目标"), this);
    targetTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_targetCombo = new QComboBox(this);
    m_targetCombo->addItem(tr("选中颜色的平均色"), 0);
    m_targetCombo->addItem(tr("第一个选中的颜色"), 1);
    targetRow->addWidget(targetTitle);
    targetRow->addWidget(m_targetCombo, 1);
    layout->addLayout(targetRow);

    if (m_animated) {
        auto *scopeRow = new QHBoxLayout;
        auto *scopeTitle = new QLabel(tr("作用范围"), this);
        scopeTitle->setObjectName(QStringLiteral("FieldLabel"));
        m_scopeCombo = new QComboBox(this);
        m_scopeCombo->addItem(tr("当前帧"), false);
        m_scopeCombo->addItem(tr("所有帧"), true);
        scopeRow->addWidget(scopeTitle);
        scopeRow->addWidget(m_scopeCombo, 1);
        layout->addLayout(scopeRow);
    }

    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("Muted"));
    m_info->setWordWrap(true);
    layout->addWidget(m_info);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    m_acceptButton = buttons->button(QDialogButtonBox::Ok);
    m_acceptButton->setText(tr("合并"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_list, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *) { updateState(); });
    connect(m_targetCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { updateState(); });

    updateState();
}

QIcon MergeColorsDialog::swatchIcon(const QColor &color) const
{
    QImage image(18, 18, QImage::Format_ARGB32);
    const Theme::Colors colors = Theme::colors();
    image.fill(colors.checkerA);
    QPainter painter(&image);
    for (int y = 0; y < 18; y += 4) {
        for (int x = 0; x < 18; x += 4) {
            if (((x / 4) + (y / 4)) % 2 == 0)
                continue;
            painter.fillRect(QRect(x, y, 4, 4), colors.checkerB);
        }
    }
    painter.fillRect(QRect(0, 0, 18, 18), color);
    painter.setPen(colors.borderStrong);
    painter.drawRect(0, 0, 17, 17);
    painter.end();
    return QIcon(QPixmap::fromImage(image));
}

QVector<QRgb> MergeColorsDialog::selectedColors() const
{
    QVector<QRgb> result;
    for (int index = 0; index < m_list->count(); ++index) {
        const QListWidgetItem *item = m_list->item(index);
        if (item->checkState() == Qt::Checked) {
            result.append(
                QRgb(item->data(Qt::UserRole).toULongLong()));
        }
    }
    return result;
}

QRgb MergeColorsDialog::targetColor() const
{
    const QVector<QRgb> selected = selectedColors();
    if (selected.isEmpty())
        return 0;
    if (m_targetCombo->currentData().toInt() == 1)
        return selected.first();

    double r = 0;
    double g = 0;
    double b = 0;
    double a = 0;
    double total = 0;
    for (QRgb color : selected) {
        double weight = 1.0;
        for (const auto &entry : m_colors) {
            if (entry.rgba == color) {
                weight = qMax(1, entry.count);
                break;
            }
        }
        r += qRed(color) * weight;
        g += qGreen(color) * weight;
        b += qBlue(color) * weight;
        a += qAlpha(color) * weight;
        total += weight;
    }
    if (total <= 0)
        return selected.first();
    return qRgba(qRound(r / total), qRound(g / total), qRound(b / total),
                 qRound(a / total));
}

bool MergeColorsDialog::applyAllFrames() const
{
    return m_scopeCombo && m_scopeCombo->currentData().toBool();
}

void MergeColorsDialog::updateState()
{
    const int count = selectedColors().size();
    m_acceptButton->setEnabled(count >= 2);
    if (count < 2) {
        m_info->setText(tr("至少勾选 2 种颜色。"));
    } else {
        const QColor target = QColor::fromRgba(targetColor());
        m_info->setText(tr("已选 %1 种颜色，合并为 %2。")
                            .arg(count)
                            .arg(target.name(QColor::HexRgb).toUpper()));
    }
}

} // namespace mcstudio
