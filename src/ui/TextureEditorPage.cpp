#include "ui/TextureEditorPage.h"

#include "ui/IconFactory.h"
#include "ui/PixelIcons.h"
#include "ui/Theme.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace mcstudio {
namespace {

QString normalizedPath(const QString &path)
{
    QString result = QDir::fromNativeSeparators(path);
    while (result.startsWith(QLatin1Char('/')))
        result.remove(0, 1);
    return result;
}

QVector<QColor> minecraftPalette()
{
    return {
        QColor(QStringLiteral("#f9fffe")), // white
        QColor(QStringLiteral("#f9801d")), // orange
        QColor(QStringLiteral("#c74ebd")), // magenta
        QColor(QStringLiteral("#3ab3da")), // light blue
        QColor(QStringLiteral("#fed83d")), // yellow
        QColor(QStringLiteral("#80c71f")), // lime
        QColor(QStringLiteral("#f38baa")), // pink
        QColor(QStringLiteral("#474f52")), // gray
        QColor(QStringLiteral("#9d9d97")), // light gray
        QColor(QStringLiteral("#169c9c")), // cyan
        QColor(QStringLiteral("#8932b8")), // purple
        QColor(QStringLiteral("#3c44aa")), // blue
        QColor(QStringLiteral("#835432")), // brown
        QColor(QStringLiteral("#5e7c16")), // green
        QColor(QStringLiteral("#b02e26")), // red
        QColor(QStringLiteral("#1d1d21")), // black
    };
}

QColor colorFromText(const QString &text)
{
    QString value = text.trimmed();
    if (value.isEmpty())
        return {};
    if (!value.startsWith(QLatin1Char('#')))
        value.prepend(QLatin1Char('#'));

    if (value.size() == 9) {
        bool ok = false;
        const uint rgba = value.mid(1).toUInt(&ok, 16);
        if (!ok)
            return {};
        return QColor(int((rgba >> 24) & 0xff), int((rgba >> 16) & 0xff),
                      int((rgba >> 8) & 0xff), int(rgba & 0xff));
    }
    const QColor color(value);
    return color.isValid() ? color : QColor();
}

QString colorToText(const QColor &color)
{
    QString text = color.name(QColor::HexRgb);
    if (color.alpha() != 255)
        text += QStringLiteral("%1").arg(color.alpha(), 2, 16, QLatin1Char('0'));
    return text;
}

} // namespace

TextureEditorPage::TextureEditorPage(const Project &project, const QString &relativePath,
                                     QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_path(normalizedPath(relativePath))
{
    QString error;
    const bool loaded = m_document.load(m_project, m_path, &error);

    buildUi();
    if (!loaded && m_dirtyBadge) {
        m_dirtyBadge->setToolTip(error);
        if (m_infoCursor)
            m_infoCursor->setText(tr("无法加载"));
    }

    m_canvas->setDocument(&m_document);
    if (m_document.isValid())
        m_canvas->setFrame(0);

    selectTool(PixelCanvas::Tool::Pen);
    setCurrentColor(m_color, false);
    rebuildRecentColors();
    updateDirtyUi();
    updateInfo();
    updateTimeline();

    QTimer::singleShot(0, this, [this] {
        if (m_canvas) {
            m_canvas->fitToView();
            m_canvas->setFocus(Qt::OtherFocusReason);
        }
    });
}

TextureEditorPage::~TextureEditorPage()
{
    stopPlayback();
    if (m_canvas)
        m_canvas->setDocument(nullptr);
}

bool TextureEditorPage::isDirty() const
{
    return m_document.dirty();
}

bool TextureEditorPage::canUndo() const
{
    return m_document.canUndo();
}

bool TextureEditorPage::canRedo() const
{
    return m_document.canRedo();
}

void TextureEditorPage::undo()
{
    stopPlayback();
    if (!m_document.undo())
        return;
    m_canvas->refresh();
    updateDirtyUi();
    updateInfo();
}

void TextureEditorPage::redo()
{
    stopPlayback();
    if (!m_document.redo())
        return;
    m_canvas->refresh();
    updateDirtyUi();
    updateInfo();
}

bool TextureEditorPage::saveDocument()
{
    if (!m_document.isValid())
        return false;

    stopPlayback();

    if (!m_document.dirty()) {
        updateDirtyUi();
        return true;
    }

    QString error;
    if (!m_document.save(m_project, &error)) {
        QMessageBox::critical(this, tr("保存失败"), error);
        return false;
    }

    updateDirtyUi();
    updateInfo();
    if (onSaved)
        onSaved();
    return true;
}

void TextureEditorPage::focusCanvas()
{
    if (m_canvas)
        m_canvas->setFocus(Qt::OtherFocusReason);
}

void TextureEditorPage::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_canvas = new PixelCanvas;
    m_canvas->setColor(m_color);

    m_playTimer = new QTimer(this);
    m_playTimer->setSingleShot(true);
    connect(m_playTimer, &QTimer::timeout, this, &TextureEditorPage::advancePlayback);

    root->addWidget(buildHeader());

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(buildToolColumn());
    body->addWidget(m_canvas, 1);
    body->addWidget(buildSidePanel());
    root->addLayout(body, 1);
    root->addWidget(buildTimeline());

    m_canvas->onHover = [this](QPoint point, QColor color) {
        if (!m_infoCursor)
            return;
        if (point.x() < 0) {
            m_infoCursor->setText(QStringLiteral("—"));
            return;
        }
        QString text = QStringLiteral("%1, %2").arg(point.x()).arg(point.y());
        if (color.alpha() == 0)
            text += tr(" · 透明");
        else
            text += QStringLiteral(" · %1").arg(color.name(QColor::HexRgb));
        m_infoCursor->setText(text);
    };
    m_canvas->onColorPicked = [this](const QColor &color) {
        setCurrentColor(color, true);
    };
    m_canvas->onEdited = [this] {
        stopPlayback();
        updateDirtyUi();
        updateInfo();
    };
    m_canvas->onZoomChanged = [this](int percent) {
        if (m_zoomLabel)
            m_zoomLabel->setText(QStringLiteral("%1%").arg(percent));
        if (m_infoZoom)
            m_infoZoom->setText(QStringLiteral("%1%").arg(percent));
        if (m_zoomSlider) {
            const QSignalBlocker blocker(m_zoomSlider);
            m_zoomSlider->setValue(qBound(100, percent, 6400));
        }
    };

    auto addShortcut = [this](const QKeySequence &sequence,
                              const std::function<void()> &action) {
        auto *shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, action] {
            if (handleTextInputShortcut())
                return;
            action();
        });
    };

    addShortcut(QKeySequence(Qt::Key_B), [this] { selectTool(PixelCanvas::Tool::Pen); });
    addShortcut(QKeySequence(Qt::Key_E), [this] { selectTool(PixelCanvas::Tool::Eraser); });
    addShortcut(QKeySequence(Qt::Key_G), [this] { selectTool(PixelCanvas::Tool::Bucket); });
    addShortcut(QKeySequence(Qt::Key_I), [this] { selectTool(PixelCanvas::Tool::Eyedropper); });
    addShortcut(QKeySequence(Qt::Key_L), [this] { selectTool(PixelCanvas::Tool::Line); });
    addShortcut(QKeySequence(Qt::Key_R), [this] { selectTool(PixelCanvas::Tool::Rect); });
    addShortcut(QKeySequence(Qt::Key_O), [this] { selectTool(PixelCanvas::Tool::Ellipse); });
    addShortcut(QKeySequence(Qt::Key_M), [this] { selectTool(PixelCanvas::Tool::Select); });
    addShortcut(QKeySequence(Qt::Key_F), [this] { m_canvas->fitToView(); });
    addShortcut(QKeySequence::Copy, [this] { m_canvas->copySelectionToClipboard(); });
    addShortcut(QKeySequence::Paste, [this] { m_canvas->pasteFromClipboard(); });
    addShortcut(QKeySequence(Qt::Key_Delete), [this] { m_canvas->deleteSelection(); });
    addShortcut(QKeySequence(Qt::Key_BracketLeft), [this] {
        m_canvas->setBrushSize(qMax(1, m_canvas->brushSize() - 1));
        if (m_brushSpin)
            m_brushSpin->setValue(m_canvas->brushSize());
    });
    addShortcut(QKeySequence(Qt::Key_BracketRight), [this] {
        m_canvas->setBrushSize(qMin(64, m_canvas->brushSize() + 1));
        if (m_brushSpin)
            m_brushSpin->setValue(m_canvas->brushSize());
    });
}

QWidget *TextureEditorPage::buildHeader()
{
    auto *header = new QFrame;
    header->setObjectName(QStringLiteral("DocumentHeader"));
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(16, 9, 16, 9);
    layout->setSpacing(10);

    auto *title = new QLabel(QFileInfo(m_path).fileName());
    title->setObjectName(QStringLiteral("DocumentTitle"));
    auto *path = new QLabel(m_path);
    path->setObjectName(QStringLiteral("DocumentPath"));
    path->setToolTip(m_path);

    m_sizeLabel = new QLabel(QStringLiteral("—"));
    m_sizeLabel->setObjectName(QStringLiteral("Muted"));
    m_modeLabel = new QLabel(QStringLiteral("静态"));
    m_modeLabel->setObjectName(QStringLiteral("DocumentBadge"));
    m_dirtyBadge = new QLabel(QStringLiteral("已保存"));
    m_dirtyBadge->setObjectName(QStringLiteral("DocumentBadge"));

    m_saveButton = new QPushButton(tr("保存"));
    m_saveButton->setObjectName(QStringLiteral("PrimaryButton"));
    m_saveButton->setIcon(IconFactory::icon(IconFactory::Glyph::Save,
                                            Theme::colors().onAccent, 16));
    m_saveButton->setToolTip(tr("保存到 pack/ 覆盖层（Ctrl+S）"));
    connect(m_saveButton, &QPushButton::clicked, this, [this] {
        saveDocument();
    });

    layout->addWidget(title);
    layout->addWidget(path, 1);
    layout->addWidget(m_sizeLabel);
    layout->addWidget(m_modeLabel);
    layout->addWidget(m_dirtyBadge);
    layout->addWidget(m_saveButton);
    return header;
}

QToolButton *TextureEditorPage::makeToolButton(PixelIcons::Glyph glyph, const QString &text,
                                               const QString &tooltip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIcon(PixelIcons::icon(glyph, Theme::colors().textSoft, 18));
    button->setIconSize(QSize(18, 18));
    button->setText(text);
    button->setToolTip(tooltip);
    button->setFixedSize(48, 46);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

QWidget *TextureEditorPage::buildToolColumn()
{
    auto *column = new QFrame;
    column->setObjectName(QStringLiteral("EditorToolColumn"));
    column->setFixedWidth(58);

    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(5, 8, 5, 8);
    layout->setSpacing(4);

    auto *group = new QButtonGroup(this);
    group->setExclusive(true);

    struct ToolSpec {
        PixelIcons::Glyph glyph;
        const char *text;
        const char *tooltip;
        PixelCanvas::Tool tool;
    };
    const QVector<ToolSpec> specs = {
        {PixelIcons::Glyph::Pen, "笔", QT_TR_NOOP("像素笔 (B)"), PixelCanvas::Tool::Pen},
        {PixelIcons::Glyph::Eraser, "擦", QT_TR_NOOP("橡皮 (E)"), PixelCanvas::Tool::Eraser},
        {PixelIcons::Glyph::Bucket, "桶", QT_TR_NOOP("油漆桶 (G)"), PixelCanvas::Tool::Bucket},
        {PixelIcons::Glyph::Dropper, "吸", QT_TR_NOOP("取色器 (I)"),
         PixelCanvas::Tool::Eyedropper},
        {PixelIcons::Glyph::Line, "线", QT_TR_NOOP("直线 (L)"), PixelCanvas::Tool::Line},
        {PixelIcons::Glyph::Rect, "矩", QT_TR_NOOP("矩形 (R)"), PixelCanvas::Tool::Rect},
        {PixelIcons::Glyph::Ellipse, "圆", QT_TR_NOOP("椭圆 (O)"), PixelCanvas::Tool::Ellipse},
        {PixelIcons::Glyph::Select, "选", QT_TR_NOOP("矩形选区 (M)"), PixelCanvas::Tool::Select},
    };

    for (const ToolSpec &spec : specs) {
        auto *button = makeToolButton(spec.glyph, tr(spec.text), tr(spec.tooltip),
                                      column);
        group->addButton(button);
        m_toolButtons.append(button);
        m_toolGlyphs.append(spec.glyph);
        const PixelCanvas::Tool tool = spec.tool;
        connect(button, &QToolButton::clicked, this, [this, tool] {
            selectTool(tool);
            m_canvas->setFocus(Qt::OtherFocusReason);
        });
        layout->addWidget(button, 0, Qt::AlignHCenter);
    }

    layout->addStretch(1);

    auto *divider = new QFrame;
    divider->setObjectName(QStringLiteral("Divider"));
    divider->setFixedHeight(1);
    layout->addWidget(divider);

    m_undoButton = new QToolButton(column);
    m_undoButton->setIcon(PixelIcons::icon(PixelIcons::Glyph::Undo,
                                           Theme::colors().textSoft, 18));
    m_undoButton->setIconSize(QSize(18, 18));
    m_undoButton->setToolTip(tr("撤销 (Ctrl+Z)"));
    m_undoButton->setAutoRaise(true);
    m_undoButton->setFixedSize(40, 32);
    m_undoButton->setFocusPolicy(Qt::NoFocus);
    connect(m_undoButton, &QToolButton::clicked, this, [this] { undo(); });
    layout->addWidget(m_undoButton, 0, Qt::AlignHCenter);

    m_redoButton = new QToolButton(column);
    m_redoButton->setIcon(PixelIcons::icon(PixelIcons::Glyph::Redo,
                                           Theme::colors().textSoft, 18));
    m_redoButton->setIconSize(QSize(18, 18));
    m_redoButton->setToolTip(tr("重做 (Ctrl+Y)"));
    m_redoButton->setAutoRaise(true);
    m_redoButton->setFixedSize(40, 32);
    m_redoButton->setFocusPolicy(Qt::NoFocus);
    connect(m_redoButton, &QToolButton::clicked, this, [this] { redo(); });
    layout->addWidget(m_redoButton, 0, Qt::AlignHCenter);

    return column;
}

QPushButton *TextureEditorPage::makePaletteButton(const QColor &color, QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setFixedSize(24, 24);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolTip(color.name(QColor::HexRgb).toUpper());
    button->setStyleSheet(
        QStringLiteral("QPushButton { background-color: %1; border: 1px solid %2; "
                       "border-radius: 3px; }"
                       "QPushButton:hover { border-color: %3; }")
            .arg(color.name(QColor::HexRgb), Theme::colors().borderStrong.name(),
                 Theme::colors().accent.name()));
    connect(button, &QPushButton::clicked, this, [this, color] {
        setCurrentColor(color, true);
    });
    return button;
}

QWidget *TextureEditorPage::buildSidePanel()
{
    auto *panel = new QFrame;
    panel->setObjectName(QStringLiteral("EditorSidePanel"));
    panel->setMinimumWidth(230);

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(7);

    auto *colorTitle = new QLabel(tr("颜色"));
    colorTitle->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(colorTitle);

    auto *colorRow = new QHBoxLayout;
    colorRow->setSpacing(6);
    m_colorSwatch = new QPushButton;
    m_colorSwatch->setFixedSize(52, 28);
    m_colorSwatch->setToolTip(tr("选择颜色（含透明度）"));
    connect(m_colorSwatch, &QPushButton::clicked, this, [this] {
        const QColor chosen = QColorDialog::getColor(m_color, this, tr("选择颜色"),
                                                     QColorDialog::ShowAlphaChannel);
        if (chosen.isValid())
            setCurrentColor(chosen, true);
    });
    m_hexEdit = new QLineEdit;
    m_hexEdit->setMaxLength(9);
    m_hexEdit->setToolTip(tr("支持 #RRGGBB 或 #RRGGBBAA"));
    connect(m_hexEdit, &QLineEdit::editingFinished, this, [this] {
        const QColor color = colorFromText(m_hexEdit->text());
        if (color.isValid())
            setCurrentColor(color, true);
        else
            updateColorUi();
    });
    colorRow->addWidget(m_colorSwatch);
    colorRow->addWidget(m_hexEdit, 1);
    layout->addLayout(colorRow);

    auto *alphaRow = new QHBoxLayout;
    alphaRow->setSpacing(6);
    auto *alphaLabel = new QLabel(QStringLiteral("A"));
    alphaLabel->setObjectName(QStringLiteral("FieldLabel"));
    m_alphaSlider = new QSlider(Qt::Horizontal);
    m_alphaSlider->setRange(0, 255);
    m_alphaSlider->setValue(255);
    m_alphaSpin = new QSpinBox;
    m_alphaSpin->setRange(0, 255);
    m_alphaSpin->setValue(255);
    m_alphaSpin->setFixedWidth(58);
    alphaRow->addWidget(alphaLabel);
    alphaRow->addWidget(m_alphaSlider, 1);
    alphaRow->addWidget(m_alphaSpin);
    layout->addLayout(alphaRow);

    const auto applyAlpha = [this](int alpha) {
        if (m_syncingColor)
            return;
        QColor color = m_color;
        color.setAlpha(qBound(0, alpha, 255));
        setCurrentColor(color, false);
    };
    connect(m_alphaSlider, &QSlider::valueChanged, this, applyAlpha);
    connect(m_alphaSpin, &QSpinBox::valueChanged, this, applyAlpha);

    auto *paletteWidget = new QWidget;
    m_paletteLayout = new QGridLayout(paletteWidget);
    m_paletteLayout->setContentsMargins(0, 0, 0, 0);
    m_paletteLayout->setSpacing(3);
    const QVector<QColor> palette = minecraftPalette();
    for (int index = 0; index < palette.size(); ++index) {
        m_paletteLayout->addWidget(makePaletteButton(palette.at(index), paletteWidget),
                                   index / 8, index % 8);
    }
    layout->addWidget(paletteWidget);

    auto *recentLabel = new QLabel(tr("最近"));
    recentLabel->setObjectName(QStringLiteral("FieldLabel"));
    m_recentLabel = recentLabel;
    layout->addWidget(recentLabel);

    auto *recentWidget = new QWidget;
    m_recentWidget = recentWidget;
    m_recentLayout = new QGridLayout(recentWidget);
    m_recentLayout->setContentsMargins(0, 0, 0, 0);
    m_recentLayout->setSpacing(3);
    layout->addWidget(recentWidget);

    auto *divider = new QFrame;
    divider->setObjectName(QStringLiteral("Divider"));
    divider->setFixedHeight(1);
    layout->addWidget(divider);

    auto *optionsTitle = new QLabel(tr("工具选项"));
    optionsTitle->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(optionsTitle);

    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(6);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_brushSpin = new QSpinBox;
    m_brushSpin->setRange(1, 64);
    m_brushSpin->setValue(1);
    m_brushSpin->setToolTip(tr("笔刷尺寸（[ / ] 调整）"));
    connect(m_brushSpin, &QSpinBox::valueChanged, this, [this](int value) {
        m_canvas->setBrushSize(value);
    });
    form->addRow(tr("笔刷"), m_brushSpin);

    m_contiguousCheck = new QCheckBox(tr("连续区域"));
    m_contiguousCheck->setChecked(true);
    m_contiguousCheck->setToolTip(tr("关闭后填充全图中所有相同颜色"));
    connect(m_contiguousCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_canvas->setBucketContiguous(checked);
    });
    form->addRow(tr("油漆桶"), m_contiguousCheck);

    m_toleranceSpin = new QSpinBox;
    m_toleranceSpin->setRange(0, 255);
    m_toleranceSpin->setValue(0);
    m_toleranceSpin->setToolTip(tr("0 = 只填充完全相同颜色"));
    connect(m_toleranceSpin, &QSpinBox::valueChanged, this, [this](int value) {
        m_canvas->setBucketTolerance(value);
    });
    form->addRow(tr("颜色容差"), m_toleranceSpin);

    m_fillCheck = new QCheckBox(tr("形状填充"));
    connect(m_fillCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_canvas->setShapeFill(checked);
    });
    form->addRow(tr("形状"), m_fillCheck);

    m_symmetryCombo = new QComboBox;
    m_symmetryCombo->addItem(tr("关闭"), 0);
    m_symmetryCombo->addItem(tr("左右"), 1);
    m_symmetryCombo->addItem(tr("上下"), 2);
    m_symmetryCombo->addItem(tr("四向"), 3);
    connect(m_symmetryCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const int mode = m_symmetryCombo->currentData().toInt();
        m_canvas->setSymmetry(static_cast<PixelEdit::Symmetry>(mode));
    });
    form->addRow(tr("对称"), m_symmetryCombo);

    m_gridCheck = new QCheckBox(tr("像素网格"));
    connect(m_gridCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_canvas->setShowGrid(checked);
    });
    form->addRow(tr("辅助"), m_gridCheck);

    m_onionCheck = new QCheckBox(tr("洋葱皮"));
    m_onionCheck->setToolTip(tr("叠加显示上一帧和下一帧"));
    connect(m_onionCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_canvas->setOnionSkin(checked);
    });
    form->addRow(QString(), m_onionCheck);
    layout->addLayout(form);

    auto *divider2 = new QFrame;
    divider2->setObjectName(QStringLiteral("Divider"));
    divider2->setFixedHeight(1);
    layout->addWidget(divider2);

    auto *infoTitle = new QLabel(tr("信息"));
    infoTitle->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(infoTitle);

    auto *infoForm = new QFormLayout;
    infoForm->setContentsMargins(0, 0, 0, 0);
    infoForm->setHorizontalSpacing(10);
    infoForm->setVerticalSpacing(4);
    m_infoSize = new QLabel(QStringLiteral("—"));
    m_infoSize->setObjectName(QStringLiteral("Muted"));
    m_infoFrame = new QLabel(QStringLiteral("—"));
    m_infoFrame->setObjectName(QStringLiteral("Muted"));
    m_infoZoom = new QLabel(QStringLiteral("—"));
    m_infoZoom->setObjectName(QStringLiteral("Muted"));
    m_infoCursor = new QLabel(QStringLiteral("—"));
    m_infoCursor->setObjectName(QStringLiteral("Muted"));
    m_infoCursor->setWordWrap(true);
    infoForm->addRow(tr("尺寸"), m_infoSize);
    infoForm->addRow(tr("帧"), m_infoFrame);
    infoForm->addRow(tr("缩放"), m_infoZoom);
    infoForm->addRow(tr("光标"), m_infoCursor);
    layout->addLayout(infoForm);

    auto *zoomRow = new QHBoxLayout;
    zoomRow->setSpacing(6);
    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(100, 6400);
    m_zoomSlider->setSingleStep(100);
    m_zoomSlider->setPageStep(400);
    m_zoomSlider->setValue(100);
    m_zoomLabel = new QLabel(QStringLiteral("100%"));
    m_zoomLabel->setObjectName(QStringLiteral("Muted"));
    m_zoomLabel->setMinimumWidth(44);
    m_zoomLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int value) {
        m_canvas->setZoomPercent(value);
    });
    auto *fitButton = new QPushButton(tr("适应"));
    fitButton->setObjectName(QStringLiteral("GhostButton"));
    fitButton->setToolTip(tr("适应窗口 (F)"));
    connect(fitButton, &QPushButton::clicked, this, [this] { m_canvas->fitToView(); });
    zoomRow->addWidget(m_zoomSlider, 1);
    zoomRow->addWidget(m_zoomLabel);
    zoomRow->addWidget(fitButton);
    layout->addLayout(zoomRow);

    layout->addStretch(1);

    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("EditorSidePanelScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFixedWidth(252);
    scroll->setWidget(panel);
    return scroll;
}

QWidget *TextureEditorPage::buildTimeline()
{
    auto *timeline = new QFrame;
    timeline->setObjectName(QStringLiteral("EditorTimeline"));
    timeline->setFixedHeight(126);

    auto *outer = new QVBoxLayout(timeline);
    outer->setContentsMargins(10, 4, 10, 5);
    outer->setSpacing(3);

    auto *top = new QHBoxLayout;
    top->setSpacing(8);
    auto *label = new QLabel(tr("帧"));
    label->setObjectName(QStringLiteral("SectionTitle"));
    top->addWidget(label);

    m_timelineScroll = new QScrollArea;
    m_timelineScroll->setObjectName(QStringLiteral("EditorTimelineScroll"));
    m_timelineScroll->setFrameShape(QFrame::NoFrame);
    m_timelineScroll->setWidgetResizable(true);
    m_timelineScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_timelineScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_timelineScroll->setFixedHeight(54);

    auto *inner = new QWidget;
    m_timelineLayout = new QHBoxLayout(inner);
    m_timelineLayout->setContentsMargins(0, 0, 0, 0);
    m_timelineLayout->setSpacing(4);
    m_timelineScroll->setWidget(inner);
    top->addWidget(m_timelineScroll, 1);

    m_timelineTop = new QWidget;
    m_timelineTop->setLayout(top);
    outer->addWidget(m_timelineTop);

    m_staticAnimationBox = new QWidget;
    auto *staticLayout = new QHBoxLayout(m_staticAnimationBox);
    staticLayout->setContentsMargins(0, 0, 0, 0);
    staticLayout->setSpacing(6);

    m_createAnimationButton = new QPushButton(tr("创建动画…"));
    m_createAnimationButton->setObjectName(QStringLiteral("GhostButton"));
    m_createAnimationButton->setToolTip(tr("复制当前帧为多帧垂直帧条，并写入 .mcmeta"));
    connect(m_createAnimationButton, &QPushButton::clicked, this,
            &TextureEditorPage::createAnimationInteractive);
    staticLayout->addWidget(m_createAnimationButton);

    m_createFromStripButton = new QPushButton(tr("按现有帧条拆分"));
    m_createFromStripButton->setObjectName(QStringLiteral("GhostButton"));
    m_createFromStripButton->setToolTip(tr("当前 PNG 高度是宽度的整数倍，可以拆成动画帧"));
    connect(m_createFromStripButton, &QPushButton::clicked, this,
            &TextureEditorPage::createAnimationFromStrip);
    staticLayout->addWidget(m_createFromStripButton);

    auto *staticHint = new QLabel(tr("MC 动画使用垂直帧条和 .mcmeta；正方形帧。"));
    staticHint->setObjectName(QStringLiteral("PanelHint"));
    staticLayout->addWidget(staticHint);
    staticLayout->addStretch(1);
    outer->addWidget(m_staticAnimationBox);

    m_animatedAnimationBox = new QWidget;
    auto *animatedLayout = new QVBoxLayout(m_animatedAnimationBox);
    animatedLayout->setContentsMargins(0, 0, 0, 0);
    animatedLayout->setSpacing(3);

    auto *animationRow = new QHBoxLayout;
    animationRow->setSpacing(6);
    m_animationInfo = new QLabel;
    m_animationInfo->setObjectName(QStringLiteral("PanelHint"));
    animationRow->addWidget(m_animationInfo);

    auto *timeLabel = new QLabel(tr("帧时长"));
    timeLabel->setObjectName(QStringLiteral("FieldLabel"));
    animationRow->addWidget(timeLabel);
    m_frameTimeSpin = new QSpinBox;
    m_frameTimeSpin->setRange(1, 1000);
    m_frameTimeSpin->setToolTip(tr("当前帧在游戏里停留的 tick 数（1 tick = 50ms）"));
    connect(m_frameTimeSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_syncingAnimation || !m_document.animated())
            return;
        m_document.setFrameTime(m_canvas->frame(), value);
        refreshAnimationEdit();
    });
    animationRow->addWidget(m_frameTimeSpin);

    auto *defaultLabel = new QLabel(tr("默认"));
    defaultLabel->setObjectName(QStringLiteral("FieldLabel"));
    animationRow->addWidget(defaultLabel);
    m_frametimeSpin = new QSpinBox;
    m_frametimeSpin->setRange(1, 1000);
    m_frametimeSpin->setToolTip(tr("未单独设置时长的帧使用这个默认值"));
    connect(m_frametimeSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_syncingAnimation || !m_document.animated())
            return;
        m_document.setFrametime(value);
        refreshAnimationEdit();
    });
    animationRow->addWidget(m_frametimeSpin);

    m_interpolateCheck = new QCheckBox(tr("interpolate"));
    m_interpolateCheck->setToolTip(tr("在帧之间做插值，对应 mcmeta 的 interpolate"));
    connect(m_interpolateCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_syncingAnimation || !m_document.animated())
            return;
        m_document.setInterpolate(checked);
        refreshAnimationEdit();
    });
    animationRow->addWidget(m_interpolateCheck);
    animationRow->addStretch(1);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(6);

    m_addFrameButton = new QPushButton(tr("添加帧"));
    m_addFrameButton->setObjectName(QStringLiteral("GhostButton"));
    m_addFrameButton->setToolTip(tr("复制当前帧并插入到后面"));
    connect(m_addFrameButton, &QPushButton::clicked, this,
            &TextureEditorPage::addAnimationFrame);
    buttonRow->addWidget(m_addFrameButton);

    m_removeFrameButton = new QPushButton(tr("删除帧"));
    m_removeFrameButton->setObjectName(QStringLiteral("GhostButton"));
    connect(m_removeFrameButton, &QPushButton::clicked, this,
            &TextureEditorPage::removeAnimationFrame);
    buttonRow->addWidget(m_removeFrameButton);

    m_moveLeftButton = new QPushButton(QStringLiteral("←"));
    m_moveLeftButton->setObjectName(QStringLiteral("GhostButton"));
    m_moveLeftButton->setToolTip(tr("当前帧前移"));
    connect(m_moveLeftButton, &QPushButton::clicked, this,
            [this] { moveAnimationFrame(-1); });
    buttonRow->addWidget(m_moveLeftButton);

    m_moveRightButton = new QPushButton(QStringLiteral("→"));
    m_moveRightButton->setObjectName(QStringLiteral("GhostButton"));
    m_moveRightButton->setToolTip(tr("当前帧后移"));
    connect(m_moveRightButton, &QPushButton::clicked, this,
            [this] { moveAnimationFrame(1); });
    buttonRow->addWidget(m_moveRightButton);

    m_playButton = new QPushButton(tr("播放"));
    m_playButton->setObjectName(QStringLiteral("GhostButton"));
    connect(m_playButton, &QPushButton::clicked, this,
            &TextureEditorPage::togglePlayback);
    buttonRow->addWidget(m_playButton);

    m_deleteAnimationButton = new QPushButton(tr("删除动画"));
    m_deleteAnimationButton->setObjectName(QStringLiteral("DangerButton"));
    connect(m_deleteAnimationButton, &QPushButton::clicked, this,
            &TextureEditorPage::deleteAnimationInteractive);
    buttonRow->addWidget(m_deleteAnimationButton);
    buttonRow->addStretch(1);

    animatedLayout->addLayout(animationRow);
    animatedLayout->addLayout(buttonRow);
    outer->addWidget(m_animatedAnimationBox);
    m_animatedAnimationBox->setVisible(false);

    m_timeline = timeline;
    m_timeline->setVisible(true);
    return timeline;
}

void TextureEditorPage::selectTool(PixelCanvas::Tool tool)
{
    if (!m_canvas)
        return;
    m_canvas->setTool(tool);
    const int index = static_cast<int>(tool);
    if (index >= 0 && index < m_toolButtons.size())
        m_toolButtons.at(index)->setChecked(true);
    updateToolOptions();
}

void TextureEditorPage::updateToolOptions()
{
    const PixelCanvas::Tool tool = m_canvas->tool();
    const bool brush = tool == PixelCanvas::Tool::Pen || tool == PixelCanvas::Tool::Eraser
                       || tool == PixelCanvas::Tool::Line || tool == PixelCanvas::Tool::Rect
                       || tool == PixelCanvas::Tool::Ellipse;
    const bool bucket = tool == PixelCanvas::Tool::Bucket;
    const bool shape = tool == PixelCanvas::Tool::Rect || tool == PixelCanvas::Tool::Ellipse;

    if (m_brushSpin)
        m_brushSpin->setEnabled(brush);
    if (m_contiguousCheck)
        m_contiguousCheck->setEnabled(bucket);
    if (m_toleranceSpin)
        m_toleranceSpin->setEnabled(bucket);
    if (m_fillCheck)
        m_fillCheck->setEnabled(shape);
}

void TextureEditorPage::setCurrentColor(const QColor &color, bool remember)
{
    if (!color.isValid())
        return;
    m_color = color;
    m_canvas->setColor(color);
    updateColorUi();
    if (remember)
        pushRecentColor(color);
}

void TextureEditorPage::updateColorUi()
{
    if (!m_colorSwatch || !m_hexEdit)
        return;

    m_syncingColor = true;
    const QSignalBlocker sliderBlocker(m_alphaSlider);
    const QSignalBlocker spinBlocker(m_alphaSpin);
    const QSignalBlocker hexBlocker(m_hexEdit);

    const QString alpha = QString::number(m_color.alphaF(), 'f', 3);
    m_colorSwatch->setStyleSheet(
        QStringLiteral("QPushButton { background-color: rgba(%1, %2, %3, %4); "
                       "border: 1px solid %5; border-radius: 4px; }"
                       "QPushButton:hover { border-color: %6; }")
            .arg(m_color.red())
            .arg(m_color.green())
            .arg(m_color.blue())
            .arg(alpha)
            .arg(Theme::colors().borderStrong.name(), Theme::colors().accent.name()));
    m_colorSwatch->setToolTip(colorToText(m_color));

    m_hexEdit->setText(colorToText(m_color));
    m_alphaSlider->setValue(m_color.alpha());
    m_alphaSpin->setValue(m_color.alpha());
    m_syncingColor = false;
}

void TextureEditorPage::pushRecentColor(const QColor &color)
{
    if (!color.isValid() || color.alpha() == 0)
        return;
    for (int index = m_recentColors.size() - 1; index >= 0; --index) {
        if (m_recentColors.at(index) == color)
            m_recentColors.removeAt(index);
    }
    m_recentColors.prepend(color);
    while (m_recentColors.size() > 16)
        m_recentColors.removeLast();
    rebuildRecentColors();
}

void TextureEditorPage::rebuildRecentColors()
{
    if (!m_recentLayout)
        return;
    clearLayout(m_recentLayout);

    const bool empty = m_recentColors.isEmpty();
    if (m_recentLabel)
        m_recentLabel->setVisible(!empty);
    if (m_recentWidget)
        m_recentWidget->setVisible(!empty);
    if (empty)
        return;

    for (int index = 0; index < m_recentColors.size(); ++index) {
        m_recentLayout->addWidget(
            makePaletteButton(m_recentColors.at(index), m_recentLayout->parentWidget()),
            index / 8, index % 8);
    }
}

void TextureEditorPage::clearLayout(QLayout *layout)
{
    if (!layout)
        return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget())
            delete widget;
        else if (QLayout *child = item->layout()) {
            clearLayout(child);
            delete child;
        }
        delete item;
    }
}

void TextureEditorPage::updateDirtyUi()
{
    const bool valid = m_document.isValid();
    const bool dirty = valid && m_document.dirty();

    if (m_dirtyBadge) {
        if (!valid) {
            m_dirtyBadge->setText(tr("无法加载"));
            m_dirtyBadge->setStyleSheet(
                QStringLiteral("color: %1;").arg(Theme::colors().danger.name()));
        } else {
            m_dirtyBadge->setText(dirty ? tr("未保存") : tr("已保存"));
            m_dirtyBadge->setStyleSheet(
                QStringLiteral("color: %1;")
                    .arg(dirty ? Theme::colors().warning.name()
                               : Theme::colors().success.name()));
        }
    }

    if (m_saveButton)
        m_saveButton->setEnabled(valid && dirty);
    if (m_undoButton)
        m_undoButton->setEnabled(valid && m_document.canUndo());
    if (m_redoButton)
        m_redoButton->setEnabled(valid && m_document.canRedo());

    if (dirty != m_lastDirty) {
        m_lastDirty = dirty;
        if (onDirtyChanged)
            onDirtyChanged(dirty);
    }
    if (onHistoryChanged)
        onHistoryChanged();
}

void TextureEditorPage::updateInfo()
{
    if (!m_document.isValid()) {
        if (m_sizeLabel)
            m_sizeLabel->setText(QStringLiteral("—"));
        if (m_modeLabel)
            m_modeLabel->setText(tr("无法加载"));
        if (m_infoSize)
            m_infoSize->setText(QStringLiteral("—"));
        if (m_infoFrame)
            m_infoFrame->setText(QStringLiteral("—"));
        if (m_infoZoom)
            m_infoZoom->setText(QStringLiteral("—"));
        if (m_timeline)
            m_timeline->setVisible(false);
        updateAnimationPanel();
        return;
    }

    const QSize frame = m_document.frameSize();
    const QSize texture = m_document.textureSize();
    const bool animated = m_document.animated();

    if (m_sizeLabel) {
        m_sizeLabel->setText(animated
                                 ? tr("%1 × %2 · 帧 %3 × %3")
                                       .arg(texture.width())
                                       .arg(texture.height())
                                       .arg(frame.width())
                                 : QStringLiteral("%1 × %2").arg(frame.width()).arg(frame.height()));
    }
    if (m_modeLabel)
        m_modeLabel->setText(animated ? tr("动画 %1 帧").arg(m_document.frameCount())
                                      : tr("静态"));

    if (m_infoSize)
        m_infoSize->setText(QStringLiteral("%1 × %2").arg(frame.width()).arg(frame.height()));
    if (m_infoFrame) {
        m_infoFrame->setText(animated
                                 ? tr("%1 / %2（物理 %3）")
                                       .arg(m_canvas->frame() + 1)
                                       .arg(m_document.frameCount())
                                       .arg(m_document.physicalFrameCount())
                                 : tr("静态"));
    }
    if (m_infoZoom)
        m_infoZoom->setText(QStringLiteral("%1%").arg(m_canvas->zoomPercent()));

    if (m_onionCheck) {
        const bool canOnion = animated && m_document.frameCount() > 1;
        m_onionCheck->setEnabled(canOnion);
        if (!canOnion && m_onionCheck->isChecked()) {
            m_onionCheck->setChecked(false);
            m_canvas->setOnionSkin(false);
        }
    }

    if (m_lastFrameCount != m_document.frameCount() || m_lastAnimated != animated) {
        m_lastFrameCount = m_document.frameCount();
        m_lastAnimated = animated;
        updateTimeline();
    }
    updateAnimationPanel();
}

void TextureEditorPage::updateAnimationPanel()
{
    if (!m_staticAnimationBox || !m_animatedAnimationBox)
        return;

    const bool valid = m_document.isValid();
    const bool animated = valid && m_document.animated();
    m_staticAnimationBox->setVisible(valid && !animated);
    m_animatedAnimationBox->setVisible(valid && animated);
    if (!valid)
        return;

    if (!animated) {
        const QSize frame = m_document.frameSize();
        const bool canSplit = frame.width() > 0 && frame.height() % frame.width() == 0
                              && frame.height() / frame.width() >= 2;
        const bool square = frame.width() > 0 && frame.width() == frame.height();
        m_createAnimationButton->setEnabled(square || canSplit);
        m_createFromStripButton->setVisible(canSplit);
        if (canSplit) {
            m_createFromStripButton->setText(
                tr("按现有帧条拆分（%1 帧）").arg(frame.height() / frame.width()));
        }
        return;
    }

    const int count = m_document.frameCount();
    const int current = m_canvas->frame();
    m_animationInfo->setText(
        tr("%1 帧 · %2 个物理帧 · 当前 #%3")
            .arg(count)
            .arg(m_document.physicalFrameCount())
            .arg(current + 1));

    m_syncingAnimation = true;
    m_frameTimeSpin->setValue(m_document.frameTime(current));
    m_frametimeSpin->setValue(m_document.frametime());
    m_interpolateCheck->setChecked(m_document.interpolate());
    m_syncingAnimation = false;

    m_removeFrameButton->setEnabled(count > 2);
    m_moveLeftButton->setEnabled(current > 0);
    m_moveRightButton->setEnabled(current + 1 < count);
    m_playButton->setEnabled(count > 1);
    m_playButton->setText(m_playing ? tr("暂停") : tr("播放"));
}

void TextureEditorPage::createAnimationInteractive()
{
    if (!m_document.isValid() || m_document.animated())
        return;

    const QSize frame = m_document.frameSize();
    if (frame.width() > 0 && frame.height() % frame.width() == 0
        && frame.height() / frame.width() >= 2) {
        createAnimationFromStrip();
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("创建动画"));
    auto *layout = new QVBoxLayout(&dialog);

    auto *hint = new QLabel(
        tr("每一帧会复制当前贴图。Minecraft 动画使用垂直帧条，帧必须是正方形。"));
    hint->setObjectName(QStringLiteral("PanelHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *form = new QFormLayout;
    auto *countSpin = new QSpinBox;
    countSpin->setRange(2, 256);
    countSpin->setValue(4);
    countSpin->setSuffix(tr(" 帧"));
    auto *timeSpin = new QSpinBox;
    timeSpin->setRange(1, 1000);
    timeSpin->setValue(1);
    timeSpin->setSuffix(tr(" tick"));
    form->addRow(tr("帧数"), countSpin);
    form->addRow(tr("默认帧时长"), timeSpin);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("创建"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    QString error;
    if (!m_document.createAnimation(countSpin->value(), timeSpin->value(), &error)) {
        QMessageBox::warning(this, tr("无法创建动画"), error);
        return;
    }

    stopPlayback();
    m_canvas->setFrame(0);
    refreshAnimationEdit();
}

void TextureEditorPage::createAnimationFromStrip()
{
    if (!m_document.isValid() || m_document.animated())
        return;

    const QSize frame = m_document.frameSize();
    if (frame.width() <= 0 || frame.height() % frame.width() != 0)
        return;
    const int count = frame.height() / frame.width();
    if (count < 2)
        return;

    const auto answer = QMessageBox::question(
        this, tr("拆分动画"),
        tr("把当前 %1×%2 的垂直帧条拆成 %3 帧动画，并写入 .mcmeta？")
            .arg(frame.width())
            .arg(frame.height())
            .arg(count),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!m_document.createAnimationFromStrip(&error)) {
        QMessageBox::warning(this, tr("无法拆分动画"), error);
        return;
    }

    stopPlayback();
    m_canvas->setFrame(0);
    refreshAnimationEdit();
}

void TextureEditorPage::deleteAnimationInteractive()
{
    if (!m_document.isValid() || !m_document.animated())
        return;

    const auto answer = QMessageBox::question(
        this, tr("删除动画"),
        tr("删除动画会保留第一帧、裁掉其余帧，并从 .mcmeta 移除 animation；"
           "gui、blur 等其他字段会保留。\n\n继续？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    stopPlayback();
    QString error;
    if (!m_document.deleteAnimation(&error)) {
        QMessageBox::warning(this, tr("无法删除动画"), error);
        return;
    }
    m_canvas->setFrame(0);
    refreshAnimationEdit();
}

void TextureEditorPage::addAnimationFrame()
{
    if (!m_document.animated())
        return;
    stopPlayback();
    const int current = m_canvas->frame();
    if (!m_document.addFrame(current))
        return;
    m_canvas->setFrame(current + 1);
    refreshAnimationEdit();
}

void TextureEditorPage::removeAnimationFrame()
{
    if (!m_document.animated())
        return;
    stopPlayback();
    const int current = m_canvas->frame();
    QString error;
    if (!m_document.removeFrame(current)) {
        QMessageBox::information(this, tr("删除帧"),
                                 tr("动画至少需要保留 2 帧。\n"
                                    "如果只想保留第一帧，请使用“删除动画”。"));
        return;
    }
    m_canvas->setFrame(qMin(current, m_document.frameCount() - 1));
    refreshAnimationEdit();
}

void TextureEditorPage::moveAnimationFrame(int delta)
{
    if (!m_document.animated())
        return;
    stopPlayback();
    const int from = m_canvas->frame();
    const int to = from + delta;
    if (!m_document.moveFrame(from, to))
        return;
    m_canvas->setFrame(to);
    refreshAnimationEdit();
}

void TextureEditorPage::togglePlayback()
{
    if (m_playing) {
        stopPlayback();
        return;
    }
    if (!m_document.animated() || m_document.frameCount() < 2)
        return;

    m_playing = true;
    if (m_playButton)
        m_playButton->setText(tr("暂停"));
    m_playTimer->start(qMax(50, m_document.frameTime(m_canvas->frame()) * 50));
}

void TextureEditorPage::advancePlayback()
{
    if (!m_playing)
        return;
    if (!m_document.animated() || m_document.frameCount() < 2) {
        stopPlayback();
        return;
    }

    const int next = (m_canvas->frame() + 1) % m_document.frameCount();
    selectFrame(next);
    m_playTimer->start(qMax(50, m_document.frameTime(next) * 50));
}

void TextureEditorPage::stopPlayback()
{
    m_playing = false;
    if (m_playTimer)
        m_playTimer->stop();
    if (m_playButton)
        m_playButton->setText(tr("播放"));
}

void TextureEditorPage::refreshAnimationEdit()
{
    if (!m_document.isValid())
        return;
    m_canvas->refresh();
    updateDirtyUi();
    updateInfo();
    updateTimeline();
}

void TextureEditorPage::updateTimeline()
{
    if (!m_timeline || !m_timelineLayout)
        return;

    clearLayout(m_timelineLayout);

    const bool animated = m_document.isValid() && m_document.animated()
                          && m_document.frameCount() > 1;
    m_timeline->setVisible(m_document.isValid());
    if (m_timelineTop)
        m_timelineTop->setVisible(animated);
    m_timeline->setFixedHeight(animated ? 126 : 48);
    if (!animated)
        return;

    const int count = m_document.frameCount();
    for (int index = 0; index < count; ++index) {
        const QImage frame = m_document.frame(index);
        if (frame.isNull())
            continue;

        auto *button = new QToolButton(m_timelineScroll->widget());
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        button->setIcon(QIcon(QPixmap::fromImage(
            frame.scaled(36, 36, Qt::KeepAspectRatio, Qt::FastTransformation))));
        button->setIconSize(QSize(36, 36));
        button->setFixedSize(46, 46);
        button->setToolTip(tr("第 %1 帧 · %2 tick").arg(index + 1).arg(m_document.frameTime(index)));
        button->setProperty("frameIndex", index);
        button->setChecked(index == m_canvas->frame());
        button->setFocusPolicy(Qt::NoFocus);
        connect(button, &QToolButton::clicked, this, [this, index] {
            selectFrame(index);
        });
        m_timelineLayout->addWidget(button);
    }
    m_timelineLayout->addStretch(1);
    m_lastFrameCount = count;
    m_lastAnimated = true;
}

void TextureEditorPage::selectFrame(int entry)
{
    if (!m_canvas)
        return;
    m_canvas->setFrame(entry);
    if (m_timelineLayout) {
        for (int index = 0; index < m_timelineLayout->count(); ++index) {
            auto *button = qobject_cast<QToolButton *>(
                m_timelineLayout->itemAt(index)->widget());
            if (button && button->property("frameIndex").toInt() == m_canvas->frame())
                button->setChecked(true);
        }
    }
    updateInfo();
}

bool TextureEditorPage::handleTextInputShortcut() const
{
    QWidget *focus = QApplication::focusWidget();
    return qobject_cast<QLineEdit *>(focus) || qobject_cast<QAbstractSpinBox *>(focus)
           || qobject_cast<QComboBox *>(focus)
           || qobject_cast<QPlainTextEdit *>(focus);
}

void TextureEditorPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::StyleChange && event->type() != QEvent::PaletteChange)
        return;

    const Theme::Colors colors = Theme::colors();
    for (int index = 0; index < m_toolButtons.size() && index < m_toolGlyphs.size();
         ++index) {
        m_toolButtons.at(index)->setIcon(
            PixelIcons::icon(m_toolGlyphs.at(index), colors.textSoft, 18));
    }
    if (m_undoButton) {
        m_undoButton->setIcon(PixelIcons::icon(PixelIcons::Glyph::Undo,
                                               colors.textSoft, 18));
    }
    if (m_redoButton) {
        m_redoButton->setIcon(PixelIcons::icon(PixelIcons::Glyph::Redo,
                                               colors.textSoft, 18));
    }
    if (m_saveButton) {
        m_saveButton->setIcon(
            IconFactory::icon(IconFactory::Glyph::Save, colors.onAccent, 16));
    }
    updateColorUi();
    updateDirtyUi();
}

} // namespace mcstudio
