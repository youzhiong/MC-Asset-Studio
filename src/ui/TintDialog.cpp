#include "ui/TintDialog.h"

#include "core/AppPaths.h"
#include "ui/TintEditors.h"
#include "ui/Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace mcstudio {
namespace {

QImage checkerPixmap(const QImage &image, int size)
{
    const Theme::Colors colors = Theme::colors();
    QImage canvas(size, size, QImage::Format_ARGB32);
    canvas.fill(colors.checkerA);
    constexpr int cell = 8;
    QPainter checker(&canvas);
    for (int y = 0; y < size; y += cell) {
        for (int x = 0; x < size; x += cell) {
            if (((x / cell) + (y / cell)) % 2 == 0)
                continue;
            checker.fillRect(QRect(x, y, cell, cell), colors.checkerB);
        }
    }
    if (!image.isNull()) {
        const QImage scaled = image.scaled(size, size, Qt::KeepAspectRatio,
                                           Qt::FastTransformation);
        const QPoint topLeft((size - scaled.width()) / 2, (size - scaled.height()) / 2);
        checker.drawImage(topLeft, scaled);
    }
    checker.end();
    return canvas;
}

} // namespace

TintDialog::TintDialog(const QImage &base, bool animated, bool hasSelection,
                       bool hasMask, QWidget *parent)
    : QDialog(parent)
    , m_base(base)
    , m_previewBase(base)
    , m_animated(animated)
    , m_hasSelection(hasSelection)
    , m_hasMask(hasMask)
{
    if (base.width() > 256 || base.height() > 256) {
        m_previewBase = base.scaled(256, 256, Qt::KeepAspectRatio,
                                    Qt::FastTransformation);
    }
    setWindowTitle(tr("调色 — 渐变映射与曲线"));
    setMinimumWidth(880);
    setMinimumHeight(600);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 12);
    layout->setSpacing(10);

    auto *hint = new QLabel(
        tr("把材质的明暗映射成颜色：颜色带决定“最暗/中间/最亮”用什么色，"
           "曲线重新分配明暗。双击颜色带添加色标，双击色标改颜色；"
           "曲线上的点可以拖动，双击空白处加点。预览满意后再点“应用”。"),
        this);
    hint->setObjectName(QStringLiteral("PanelHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *content = new QHBoxLayout;
    content->setSpacing(18);

    auto *previewColumn = new QVBoxLayout;
    auto *originalTitle = new QLabel(tr("原图"), this);
    originalTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_originalPreview = new QLabel(this);
    m_originalPreview->setFixedSize(220, 220);
    m_originalPreview->setPixmap(QPixmap::fromImage(checkerPixmap(m_base, 220)));
    auto *resultTitle = new QLabel(tr("效果"), this);
    resultTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_resultPreview = new QLabel(this);
    m_resultPreview->setFixedSize(220, 220);
    m_resultPreview->setStyleSheet(QStringLiteral("background: transparent;"));

    previewColumn->addWidget(originalTitle);
    previewColumn->addWidget(m_originalPreview);
    previewColumn->addSpacing(6);
    previewColumn->addWidget(resultTitle);
    previewColumn->addWidget(m_resultPreview);
    previewColumn->addStretch(1);
    content->addLayout(previewColumn);

    auto *controls = new QVBoxLayout;
    controls->setSpacing(8);

    auto *presetRow = new QHBoxLayout;
    auto *presetTitle = new QLabel(tr("预设"), this);
    presetTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_presetCombo = new QComboBox(this);
    m_presetCombo->addItem(tr("自定义"), QString());
    for (const QString &id : TintRecipe::presetIds())
        m_presetCombo->addItem(TintRecipe::presetName(id), id);
    presetRow->addWidget(presetTitle);
    presetRow->addWidget(m_presetCombo, 1);
    controls->addLayout(presetRow);

    auto *rampTitle = new QLabel(tr("颜色带（暗 → 亮）"), this);
    rampTitle->setObjectName(QStringLiteral("FieldLabel"));
    controls->addWidget(rampTitle);
    m_ramp = new TintRampEditor(this);
    controls->addWidget(m_ramp);

    auto *curveTitle = new QLabel(tr("明暗曲线（横轴=原图明暗，纵轴=调整后）"), this);
    curveTitle->setObjectName(QStringLiteral("FieldLabel"));
    controls->addWidget(curveTitle);
    m_curve = new TintCurveEditor(this);
    m_curve->setMinimumHeight(190);
    controls->addWidget(m_curve);

    auto *strengthRow = new QHBoxLayout;
    auto *strengthTitle = new QLabel(tr("染色强度"), this);
    strengthTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_strengthSlider = new QSlider(Qt::Horizontal, this);
    m_strengthSlider->setRange(0, 100);
    m_strengthSlider->setValue(100);
    m_strengthLabel = new QLabel(QStringLiteral("100%"), this);
    m_strengthLabel->setObjectName(QStringLiteral("Muted"));
    m_strengthLabel->setMinimumWidth(42);
    m_strengthLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    strengthRow->addWidget(strengthTitle);
    strengthRow->addWidget(m_strengthSlider, 1);
    strengthRow->addWidget(m_strengthLabel);
    controls->addLayout(strengthRow);

    if (m_animated) {
        auto *scopeRow = new QHBoxLayout;
        auto *scopeTitle = new QLabel(tr("作用范围"), this);
        scopeTitle->setObjectName(QStringLiteral("FieldLabel"));
        m_scopeCombo = new QComboBox(this);
        m_scopeCombo->addItem(tr("当前帧"), false);
        m_scopeCombo->addItem(tr("所有帧"), true);
        scopeRow->addWidget(scopeTitle);
        scopeRow->addWidget(m_scopeCombo, 1);
        controls->addLayout(scopeRow);
    }

    m_selectionCheck = new QCheckBox(tr("只改当前选区（应用时）"), this);
    m_selectionCheck->setEnabled(m_hasSelection);
    m_selectionCheck->setToolTip(
        m_hasSelection ? tr("先用矩形选区工具框一块，再勾选这里")
                       : tr("先用矩形选区工具在画布上框一块区域"));
    connect(m_selectionCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && m_maskCheck && m_maskCheck->isChecked())
            m_maskCheck->setChecked(false);
        refreshPreview();
    });
    controls->addWidget(m_selectionCheck);

    m_maskCheck = new QCheckBox(tr("只改调色蒙版（套索/魔棒）"), this);
    m_maskCheck->setEnabled(m_hasMask);
    m_maskCheck->setToolTip(
        m_hasMask ? tr("先用“套”或“魔”在画布上圈出区域")
                  : tr("先用左侧“套”（套索）或“魔”（魔棒）圈出区域"));
    connect(m_maskCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked && m_selectionCheck && m_selectionCheck->isChecked())
            m_selectionCheck->setChecked(false);
        refreshPreview();
    });
    controls->addWidget(m_maskCheck);

    m_gradientCheck = new QCheckBox(tr("方向渐变（两套配色过渡）"), this);
    m_gradientCheck->setToolTip(tr("例如上亮下暗：两套颜色沿一个方向平滑过渡"));
    connect(m_gradientCheck, &QCheckBox::toggled, this, [this](bool) {
        const bool enabled = m_gradientCheck->isChecked();
        if (m_secondPreset)
            m_secondPreset->setEnabled(enabled);
        if (m_angleSlider)
            m_angleSlider->setEnabled(enabled);
        refreshPreview();
    });
    controls->addWidget(m_gradientCheck);

    auto *gradientRow = new QHBoxLayout;
    gradientRow->setSpacing(6);
    auto *secondTitle = new QLabel(tr("第二配色"), this);
    secondTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_secondPreset = new QComboBox(this);
    for (const QString &id : TintRecipe::presetIds())
        m_secondPreset->addItem(TintRecipe::presetName(id), id);
    m_secondPreset->setCurrentIndex(
        qMax(0, m_secondPreset->findData(QStringLiteral("diamond"))));
    m_secondPreset->setEnabled(false);
    auto *angleTitle = new QLabel(tr("角度"), this);
    angleTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_angleSlider = new QSlider(Qt::Horizontal, this);
    m_angleSlider->setRange(0, 360);
    m_angleSlider->setValue(90);
    m_angleSlider->setEnabled(false);
    m_angleLabel = new QLabel(QStringLiteral("90°"), this);
    m_angleLabel->setObjectName(QStringLiteral("Muted"));
    m_angleLabel->setMinimumWidth(38);
    m_angleLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    gradientRow->addWidget(secondTitle);
    gradientRow->addWidget(m_secondPreset, 1);
    gradientRow->addWidget(angleTitle);
    gradientRow->addWidget(m_angleSlider, 1);
    gradientRow->addWidget(m_angleLabel);
    controls->addLayout(gradientRow);

    m_secondRecipe = TintRecipe::preset(QStringLiteral("diamond"));
    connect(m_secondPreset, &QComboBox::currentIndexChanged, this, [this](int) {
        const QString id = m_secondPreset->currentData().toString();
        m_secondRecipe = TintRecipe::preset(id);
        refreshPreview();
    });
    connect(m_angleSlider, &QSlider::valueChanged, this, [this](int value) {
        m_angleLabel->setText(QStringLiteral("%1°").arg(value));
        refreshPreview();
    });

    auto *recipeRow = new QHBoxLayout;
    recipeRow->setSpacing(6);
    auto *saveButton = new QPushButton(tr("保存配方…"), this);
    saveButton->setObjectName(QStringLiteral("GhostButton"));
    connect(saveButton, &QPushButton::clicked, this, &TintDialog::saveRecipe);
    auto *loadButton = new QPushButton(tr("载入配方…"), this);
    loadButton->setObjectName(QStringLiteral("GhostButton"));
    connect(loadButton, &QPushButton::clicked, this, &TintDialog::loadRecipe);
    recipeRow->addWidget(saveButton);
    recipeRow->addWidget(loadButton);
    recipeRow->addStretch(1);
    controls->addLayout(recipeRow);
    controls->addStretch(1);
    auto *controlsWidget = new QWidget(this);
    controlsWidget->setLayout(controls);
    auto *controlsScroll = new QScrollArea(this);
    controlsScroll->setWidgetResizable(true);
    controlsScroll->setFrameShape(QFrame::NoFrame);
    controlsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    controlsScroll->setWidget(controlsWidget);
    content->addWidget(controlsScroll, 1);
    layout->addLayout(content, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    m_ramp->onChanged = [this] {
        if (m_syncing)
            return;
        m_recipe.setStops(m_ramp->stops());
        syncPresetCombo();
        refreshPreview();
    };
    m_curve->onChanged = [this] {
        if (m_syncing)
            return;
        m_recipe.setCurve(m_curve->curve());
        syncPresetCombo();
        refreshPreview();
    };
    connect(m_strengthSlider, &QSlider::valueChanged, this, [this](int value) {
        m_strengthLabel->setText(QStringLiteral("%1%").arg(value));
        if (m_syncing)
            return;
        m_recipe.setStrength(value);
        refreshPreview();
    });
    connect(m_presetCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_syncing)
            return;
        const QString id = m_presetCombo->currentData().toString();
        if (id.isEmpty())
            return;
        setRecipe(TintRecipe::preset(id), id);
    });

    setRecipe(TintRecipe::preset(QStringLiteral("gold")), QStringLiteral("gold"));
}

void TintDialog::setRecipe(const TintRecipe &recipe, const QString &presetId)
{
    m_recipe = recipe;
    m_syncing = true;
    m_ramp->setStops(m_recipe.stops());
    m_curve->setCurve(m_recipe.curve());
    m_strengthSlider->setValue(m_recipe.strength());
    m_strengthLabel->setText(QStringLiteral("%1%").arg(m_recipe.strength()));
    const QSignalBlocker blocker(m_presetCombo);
    int index = 0;
    if (!presetId.isEmpty()) {
        for (int i = 0; i < m_presetCombo->count(); ++i) {
            if (m_presetCombo->itemData(i).toString() == presetId) {
                index = i;
                break;
            }
        }
    }
    m_presetCombo->setCurrentIndex(index);
    m_syncing = false;
    refreshPreview();
}

void TintDialog::saveRecipe()
{
    QDir directory(QDir(AppPaths::dataRoot()).filePath(QStringLiteral("tints")));
    directory.mkpath(QStringLiteral("."));
    QString path = QFileDialog::getSaveFileName(
        this, tr("保存调色配方"), directory.filePath(QStringLiteral("recipe.json")),
        tr("调色配方 (*.json)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        path += QStringLiteral(".json");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("保存配方"),
                             tr("无法写入：%1").arg(path));
        return;
    }
    file.write(QJsonDocument(m_recipe.toJson()).toJson(QJsonDocument::Indented));
}

void TintDialog::loadRecipe()
{
    QDir directory(QDir(AppPaths::dataRoot()).filePath(QStringLiteral("tints")));
    const QString path = QFileDialog::getOpenFileName(
        this, tr("载入调色配方"), directory.path(), tr("调色配方 (*.json)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("载入配方"),
                             tr("无法读取：%1").arg(path));
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) {
        QMessageBox::warning(this, tr("载入配方"),
                             tr("这个文件不是有效的调色配方。"));
        return;
    }
    setRecipe(TintRecipe::fromJson(document.object()));
}

void TintDialog::syncPresetCombo()
{
    if (!m_presetCombo)
        return;
    const QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->setCurrentIndex(0);
}

bool TintDialog::applyAllFrames() const
{
    return m_scopeCombo && m_scopeCombo->currentData().toBool();
}

bool TintDialog::selectionOnly() const
{
    return m_selectionCheck && m_selectionCheck->isChecked();
}

bool TintDialog::maskOnly() const
{
    return m_maskCheck && m_maskCheck->isChecked();
}

bool TintDialog::gradientEnabled() const
{
    return m_gradientCheck && m_gradientCheck->isChecked();
}

int TintDialog::angleDegrees() const
{
    return m_angleSlider ? m_angleSlider->value() : 90;
}

QImage TintDialog::composePreview(const QImage &image) const
{
    return checkerPixmap(image, 220);
}

void TintDialog::refreshPreview()
{
    if (!m_resultPreview || m_base.isNull())
        return;
    QImage result;
    if (gradientEnabled()) {
        result = m_recipe.applyGradient(m_previewBase, m_secondRecipe,
                                        angleDegrees());
    } else {
        result = m_recipe.apply(m_previewBase);
    }
    m_resultPreview->setPixmap(QPixmap::fromImage(composePreview(result)));
}

} // namespace mcstudio
