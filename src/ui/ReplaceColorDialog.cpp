#include "ui/ReplaceColorDialog.h"

#include "ui/Theme.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace mcstudio {
namespace {

QString colorText(const QColor &color)
{
    QString text = color.name(QColor::HexRgb);
    if (color.alpha() != 255)
        text += QStringLiteral("%1").arg(color.alpha(), 2, 16, QLatin1Char('0'));
    return text;
}

void styleSwatch(QPushButton *button, const QColor &color)
{
    button->setStyleSheet(
        QStringLiteral("QPushButton { background-color: rgba(%1, %2, %3, %4); "
                       "border: 1px solid %5; border-radius: 4px; }"
                       "QPushButton:hover { border-color: %6; }")
            .arg(color.red())
            .arg(color.green())
            .arg(color.blue())
            .arg(QString::number(color.alphaF(), 'f', 3),
                 Theme::colors().borderStrong.name(), Theme::colors().accent.name()));
}

} // namespace

ReplaceColorDialog::ReplaceColorDialog(const QColor &target, bool animated,
                                       QWidget *parent)
    : QDialog(parent)
    , m_target(target)
    , m_replacement(target)
    , m_animated(animated)
{
    setWindowTitle(tr("替换颜色"));
    setMinimumWidth(400);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    auto *hint = new QLabel(
        tr("把这张材质里所有“原颜色”的像素换成“新颜色”。整次替换只算一步，"
           "可以用 Ctrl+Z 撤销。"),
        this);
    hint->setObjectName(QStringLiteral("PanelHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);

    auto *targetRow = new QWidget(this);
    auto *targetLayout = new QHBoxLayout(targetRow);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    targetLayout->setSpacing(8);
    m_targetSwatch = new QPushButton(targetRow);
    m_targetSwatch->setFixedSize(46, 24);
    m_targetSwatch->setEnabled(false);
    m_targetSwatch->setFocusPolicy(Qt::NoFocus);
    m_targetHex = new QLabel(targetRow);
    m_targetHex->setObjectName(QStringLiteral("Muted"));
    targetLayout->addWidget(m_targetSwatch);
    targetLayout->addWidget(m_targetHex);
    targetLayout->addStretch(1);
    form->addRow(tr("原颜色"), targetRow);

    auto *newRow = new QWidget(this);
    auto *newLayout = new QHBoxLayout(newRow);
    newLayout->setContentsMargins(0, 0, 0, 0);
    newLayout->setSpacing(8);
    m_newSwatch = new QPushButton(newRow);
    m_newSwatch->setFixedSize(46, 24);
    m_newSwatch->setToolTip(tr("点击选择新颜色（含透明度）"));
    m_newSwatch->setCursor(Qt::PointingHandCursor);
    connect(m_newSwatch, &QPushButton::clicked, this, &ReplaceColorDialog::pickReplacement);
    m_newHex = new QLabel(newRow);
    m_newHex->setObjectName(QStringLiteral("Muted"));
    newLayout->addWidget(m_newSwatch);
    newLayout->addWidget(m_newHex);
    newLayout->addStretch(1);
    form->addRow(tr("新颜色"), newRow);

    m_toleranceSpin = new QSpinBox(this);
    m_toleranceSpin->setRange(0, 255);
    m_toleranceSpin->setValue(0);
    m_toleranceSpin->setToolTip(
        tr("0 = 只替换完全相同的颜色；数值越大，越接近的颜色也会一起替换"));
    connect(m_toleranceSpin, &QSpinBox::valueChanged, this,
            [this](int) { refreshCount(); });
    form->addRow(tr("颜色容差"), m_toleranceSpin);

    m_scopeCombo = new QComboBox(this);
    m_scopeCombo->addItem(tr("所有帧"), true);
    m_scopeCombo->addItem(tr("仅当前帧"), false);
    connect(m_scopeCombo, &QComboBox::currentIndexChanged, this,
            [this](int) { refreshCount(); });
    if (m_animated)
        form->addRow(tr("范围"), m_scopeCombo);
    else
        m_scopeCombo->setVisible(false);

    layout->addLayout(form);

    m_countLabel = new QLabel(this);
    m_countLabel->setObjectName(QStringLiteral("Muted"));
    m_countLabel->setWordWrap(true);
    layout->addWidget(m_countLabel);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    m_acceptButton = buttons->button(QDialogButtonBox::Ok);
    m_acceptButton->setText(tr("替换"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    updateSwatches();
    refreshCount();
}

int ReplaceColorDialog::tolerance() const
{
    return m_toleranceSpin ? m_toleranceSpin->value() : 0;
}

bool ReplaceColorDialog::allFrames() const
{
    if (!m_animated || !m_scopeCombo)
        return false;
    return m_scopeCombo->currentData().toBool();
}

void ReplaceColorDialog::setCountProvider(
    std::function<int(int, bool)> provider)
{
    m_countProvider = std::move(provider);
    refreshCount();
}

void ReplaceColorDialog::pickReplacement()
{
    const QColor chosen = QColorDialog::getColor(m_replacement, this, tr("新颜色"),
                                                 QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid())
        return;
    m_replacement = chosen;
    updateSwatches();
    refreshCount();
}

void ReplaceColorDialog::updateSwatches()
{
    styleSwatch(m_targetSwatch, m_target);
    styleSwatch(m_newSwatch, m_replacement);
    m_targetHex->setText(colorText(m_target));
    m_newHex->setText(colorText(m_replacement));
}

void ReplaceColorDialog::refreshCount()
{
    if (!m_countLabel || !m_acceptButton)
        return;

    if (m_replacement.rgba() == m_target.rgba() && tolerance() == 0) {
        m_countLabel->setText(tr("新颜色和原颜色相同，不会产生变化。"));
        m_acceptButton->setEnabled(false);
        return;
    }

    int count = -1;
    if (m_countProvider)
        count = m_countProvider(tolerance(), allFrames());

    if (count == 0) {
        m_countLabel->setText(
            tr("图中没有匹配的像素。可以调高“颜色容差”，或换一个原颜色。"));
        m_acceptButton->setEnabled(false);
        return;
    }

    if (count > 0) {
        m_countLabel->setText(tr("将替换 %1 个像素%2。")
                                  .arg(count)
                                  .arg(allFrames() ? tr("（所有帧）") : QString()));
    } else {
        m_countLabel->clear();
    }
    m_acceptButton->setEnabled(true);
}

} // namespace mcstudio
