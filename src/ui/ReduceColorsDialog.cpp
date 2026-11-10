#include "ui/ReduceColorsDialog.h"

#include "core/ColorReducer.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace mcstudio {
namespace {

QImage previewImage(const QImage &image, int size)
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
            image.scaled(size, size, Qt::KeepAspectRatio, Qt::FastTransformation);
        painter.drawImage(QPoint((size - scaled.width()) / 2,
                                 (size - scaled.height()) / 2),
                          scaled);
    }
    painter.end();
    return canvas;
}

} // namespace

ReduceColorsDialog::ReduceColorsDialog(const QImage &base, bool animated,
                                       QWidget *parent)
    : QDialog(parent)
    , m_base(base)
    , m_animated(animated)
{
    setWindowTitle(tr("减少颜色"));
    setMinimumWidth(660);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    auto *hint = new QLabel(
        tr("把看起来几乎一样的颜色合并成一种，减少总色数。"
           "适合从照片/拍屏得到的材质；动画会用同一套色板，避免播放时闪烁。"),
        this);
    hint->setObjectName(QStringLiteral("PanelHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *content = new QHBoxLayout;
    content->setSpacing(18);

    auto *originalColumn = new QVBoxLayout;
    auto *originalTitle = new QLabel(tr("原图"), this);
    originalTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_originalPreview = new QLabel(this);
    m_originalPreview->setFixedSize(220, 220);
    m_originalPreview->setPixmap(QPixmap::fromImage(previewImage(m_base, 220)));
    originalColumn->addWidget(originalTitle);
    originalColumn->addWidget(m_originalPreview);
    originalColumn->addStretch(1);
    content->addLayout(originalColumn);

    auto *resultColumn = new QVBoxLayout;
    auto *resultTitle = new QLabel(tr("减少后"), this);
    resultTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_resultPreview = new QLabel(this);
    m_resultPreview->setFixedSize(220, 220);
    resultColumn->addWidget(resultTitle);
    resultColumn->addWidget(m_resultPreview);

    auto *targetRow = new QHBoxLayout;
    auto *targetTitle = new QLabel(tr("目标色数"), this);
    targetTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_targetSpin = new QSpinBox(this);
    m_targetSpin->setRange(2, 256);
    m_targetSpin->setValue(32);
    m_targetSpin->setSuffix(tr(" 色"));
    connect(m_targetSpin, &QSpinBox::valueChanged, this,
            [this](int) { refreshPreview(); });
    targetRow->addWidget(targetTitle);
    targetRow->addWidget(m_targetSpin);
    targetRow->addStretch(1);
    resultColumn->addLayout(targetRow);

    if (m_animated) {
        auto *scopeRow = new QHBoxLayout;
        auto *scopeTitle = new QLabel(tr("作用范围"), this);
        scopeTitle->setObjectName(QStringLiteral("FieldLabel"));
        m_scopeCombo = new QComboBox(this);
        m_scopeCombo->addItem(tr("当前帧"), false);
        m_scopeCombo->addItem(tr("所有帧（共用色板）"), true);
        scopeRow->addWidget(scopeTitle);
        scopeRow->addWidget(m_scopeCombo, 1);
        resultColumn->addLayout(scopeRow);
    }

    m_info = new QLabel(this);
    m_info->setObjectName(QStringLiteral("Muted"));
    m_info->setWordWrap(true);
    resultColumn->addWidget(m_info);
    resultColumn->addStretch(1);
    content->addLayout(resultColumn, 1);

    layout->addLayout(content, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    refreshPreview();
}

int ReduceColorsDialog::targetColors() const
{
    return m_targetSpin ? m_targetSpin->value() : 32;
}

bool ReduceColorsDialog::applyAllFrames() const
{
    return m_scopeCombo && m_scopeCombo->currentData().toBool();
}

void ReduceColorsDialog::refreshPreview()
{
    if (!m_resultPreview || m_base.isNull())
        return;
    const ColorReduceResult result = reduceImage(m_base, targetColors());
    m_resultPreview->setPixmap(QPixmap::fromImage(composePreview(result.image)));
    m_info->setText(tr("原图 %1 色 → 预览 %2 色")
                        .arg(result.beforeColors)
                        .arg(result.afterColors));
}

QImage ReduceColorsDialog::composePreview(const QImage &image) const
{
    return previewImage(image, 220);
}

} // namespace mcstudio
