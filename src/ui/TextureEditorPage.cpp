#include "ui/TextureEditorPage.h"

#include "core/AppPaths.h"
#include "core/ColorReducer.h"
#include "core/PaletteStore.h"
#include "core/TemplateLibrary.h"
#include "ui/ColorSwatchGrid.h"
#include "ui/ColorWheel.h"
#include "ui/EquipmentDialog.h"
#include "ui/IconFactory.h"
#include "ui/MergeColorsDialog.h"
#include "ui/PixelIcons.h"
#include "ui/ReferenceView.h"
#include "ui/ReduceColorsDialog.h"
#include "ui/ReplaceColorDialog.h"
#include "ui/TexturePickerDialog.h"
#include "ui/TemplateDialog.h"
#include "ui/Theme.h"
#include "ui/TintDialog.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QRegion>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
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
    restoreLayoutSettings();

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
    updateDirtyUi();
    updateInfo();
    updateTimeline();
    refreshColorLibrary();
    refreshTexturePalette();
    updateScaleButtons();

    QSettings settings(QDir(AppPaths::dataRoot()).filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);
    applyTileMode(settings.value(QStringLiteral("editor/tileMode"), 0).toInt());

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
    saveLayoutSettings();
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
    refreshTexturePalette();
    updateScaleButtons();
}

void TextureEditorPage::redo()
{
    stopPlayback();
    if (!m_document.redo())
        return;
    m_canvas->refresh();
    updateDirtyUi();
    updateInfo();
    refreshTexturePalette();
    updateScaleButtons();
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

    m_toolColumn = buildToolColumn();
    QWidget *sidePanel = buildSidePanel();

    m_splitter = new QSplitter(Qt::Horizontal);
    m_splitter->setObjectName(QStringLiteral("EditorSplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(4);
    m_splitter->addWidget(m_toolColumn);
    m_splitter->addWidget(m_canvas);
    m_splitter->addWidget(sidePanel);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    connect(m_splitter, &QSplitter::splitterMoved, this, [this](int, int) {
        if (m_sidePanelScroll)
            m_savedPanelWidth = m_sidePanelScroll->width();
    });

    root->addWidget(m_splitter, 1);
    root->addWidget(buildTimeline());

    setPanelOnRight(m_panelOnRight);
    applySectionVisibility();

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
        refreshTexturePalette();
        updateScaleButtons();
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
    addShortcut(QKeySequence(Qt::Key_A), [this] { selectTool(PixelCanvas::Tool::Lasso); });
    addShortcut(QKeySequence(Qt::Key_W), [this] { selectTool(PixelCanvas::Tool::MagicWand); });
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

    m_layoutButton = new QToolButton;
    m_layoutButton->setText(tr("布局"));
    m_layoutButton->setToolTip(tr("自定义编辑器布局"));
    m_layoutButton->setPopupMode(QToolButton::InstantPopup);
    m_layoutButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_layoutButton->setFocusPolicy(Qt::NoFocus);
    buildLayoutMenu(m_layoutButton);

    m_focusButton = new QToolButton;
    m_focusButton->setText(tr("专注"));
    m_focusButton->setCheckable(true);
    m_focusButton->setToolTip(tr("隐藏主界面的左右栏，让编辑区更大"));
    m_focusButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_focusButton->setAutoRaise(true);
    m_focusButton->setFocusPolicy(Qt::NoFocus);
    connect(m_focusButton, &QToolButton::toggled, this, [this](bool checked) {
        m_focusMode = checked;
        if (onFocusModeChanged)
            onFocusModeChanged(checked);
    });

    m_popOutButton = new QToolButton;
    m_popOutButton->setText(tr("独立窗口"));
    m_popOutButton->setToolTip(tr("把这张材质放到单独的窗口里编辑"));
    m_popOutButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_popOutButton->setAutoRaise(true);
    m_popOutButton->setFocusPolicy(Qt::NoFocus);
    connect(m_popOutButton, &QToolButton::clicked, this, [this] {
        if (m_windowed) {
            if (onReturnToTab)
                onReturnToTab();
        } else if (onPopOut) {
            onPopOut();
        }
    });

    m_templateButton = new QToolButton;
    m_templateButton->setText(tr("模板"));
    m_templateButton->setToolTip(tr("从灰度模板开始制作锭、矿块、装备等"));
    m_templateButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_templateButton->setAutoRaise(true);
    m_templateButton->setFocusPolicy(Qt::NoFocus);
    connect(m_templateButton, &QToolButton::clicked, this,
            &TextureEditorPage::openTemplateDialog);

    m_tintButton = new QToolButton;
    m_tintButton->setText(tr("调色"));
    m_tintButton->setToolTip(tr("用颜色带和曲线把明暗映射成颜色"));
    m_tintButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_tintButton->setAutoRaise(true);
    m_tintButton->setFocusPolicy(Qt::NoFocus);
    connect(m_tintButton, &QToolButton::clicked, this,
            &TextureEditorPage::openTintDialog);

    layout->addWidget(title);
    layout->addWidget(path, 1);
    layout->addWidget(m_sizeLabel);
    layout->addWidget(m_modeLabel);
    layout->addWidget(m_dirtyBadge);
    layout->addWidget(m_focusButton);
    layout->addWidget(m_popOutButton);
    layout->addWidget(m_templateButton);
    layout->addWidget(m_tintButton);
    layout->addWidget(m_layoutButton);
    layout->addWidget(m_saveButton);
    return header;
}

void TextureEditorPage::buildLayoutMenu(QToolButton *button)
{
    auto *menu = new QMenu(button);

    m_panelRightAction = menu->addAction(tr("把面板放在右侧"));
    m_panelRightAction->setCheckable(true);
    connect(m_panelRightAction, &QAction::triggered, this, [this] {
        setPanelOnRight(true);
    });

    m_panelLeftAction = menu->addAction(tr("把面板放在左侧"));
    m_panelLeftAction->setCheckable(true);
    connect(m_panelLeftAction, &QAction::triggered, this, [this] {
        setPanelOnRight(false);
    });

    menu->addSeparator();

    m_showColorAction = menu->addAction(tr("显示颜色面板"));
    m_showColorAction->setCheckable(true);
    connect(m_showColorAction, &QAction::triggered, this, [this](bool checked) {
        m_showColorSection = checked;
        applySectionVisibility();
        saveLayoutSettings();
    });

    m_showToolsAction = menu->addAction(tr("显示工具选项"));
    m_showToolsAction->setCheckable(true);
    connect(m_showToolsAction, &QAction::triggered, this, [this](bool checked) {
        m_showToolsSection = checked;
        applySectionVisibility();
        saveLayoutSettings();
    });

    m_showInfoAction = menu->addAction(tr("显示信息"));
    m_showInfoAction->setCheckable(true);
    connect(m_showInfoAction, &QAction::triggered, this, [this](bool checked) {
        m_showInfoSection = checked;
        applySectionVisibility();
        saveLayoutSettings();
    });

    m_showTimelineAction = menu->addAction(tr("显示帧时间轴"));
    m_showTimelineAction->setCheckable(true);
    connect(m_showTimelineAction, &QAction::triggered, this, [this](bool checked) {
        m_showTimeline = checked;
        updateTimeline();
        saveLayoutSettings();
    });

    menu->addSeparator();
    QAction *resetAction = menu->addAction(tr("恢复默认布局"));
    connect(resetAction, &QAction::triggered, this, [this] { resetLayout(); });

    button->setMenu(menu);
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
        {PixelIcons::Glyph::Select, "套", QT_TR_NOOP("套索：圈出调色蒙版 (A)"),
         PixelCanvas::Tool::Lasso},
        {PixelIcons::Glyph::Bucket, "魔", QT_TR_NOOP("魔棒：按颜色圈出调色蒙版 (W)"),
         PixelCanvas::Tool::MagicWand},
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

QWidget *TextureEditorPage::buildSidePanel()
{
    auto *panel = new QFrame;
    panel->setObjectName(QStringLiteral("EditorSidePanel"));
    panel->setMinimumWidth(230);

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    m_referenceSection = buildReferenceSection();
    layout->addWidget(m_referenceSection);

    m_colorSection = buildColorSection();
    layout->addWidget(m_colorSection);

    m_toolSection = buildToolOptions();
    layout->addWidget(m_toolSection);

    m_infoSection = buildInfoSection();
    layout->addWidget(m_infoSection);

    layout->addStretch(1);

    m_sidePanelScroll = new QScrollArea;
    m_sidePanelScroll->setObjectName(QStringLiteral("EditorSidePanelScroll"));
    m_sidePanelScroll->setWidgetResizable(true);
    m_sidePanelScroll->setFrameShape(QFrame::NoFrame);
    m_sidePanelScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sidePanelScroll->setMinimumWidth(280);
    m_sidePanelScroll->setMaximumWidth(560);
    m_sidePanelScroll->setWidget(panel);
    return m_sidePanelScroll;
}

QWidget *TextureEditorPage::buildColorSection()
{
    auto *section = new QWidget;
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);

    auto *title = new QLabel(tr("颜色"));
    title->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(title);

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
    m_alphaSpin->setFixedWidth(92);
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

    auto *wheelTitle = new QLabel(tr("色轮"));
    wheelTitle->setObjectName(QStringLiteral("FieldLabel"));
    layout->addWidget(wheelTitle);

    m_wheel = new ColorWheel;
    m_wheel->setFixedSize(196, 196);
    m_wheel->setToolTip(tr("外圈选色相，往圆心越来越淡；明暗由下面滑杆控制"));
    m_wheel->onColorChanged = [this](const QColor &color) {
        if (m_syncingColor)
            return;
        QColor next = color;
        next.setAlpha(m_color.alpha());
        setCurrentColor(next, false);
    };
    m_wheel->onColorCommitted = [this] {
        mcstudio::pushRecentColor(m_color);
        refreshColorLibrary();
    };
    layout->addWidget(m_wheel, 0, Qt::AlignHCenter);

    auto *valueRow = new QHBoxLayout;
    valueRow->setSpacing(6);
    auto *valueTitle = new QLabel(tr("明暗"));
    valueTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_valueSlider = new QSlider(Qt::Horizontal);
    m_valueSlider->setRange(0, 255);
    m_valueSlider->setPageStep(16);
    m_valueSlider->setValue(255);
    m_valueSlider->setToolTip(tr("0 = 黑，255 = 最亮"));
    m_valueLabel = new QLabel(QStringLiteral("100%"));
    m_valueLabel->setObjectName(QStringLiteral("Muted"));
    m_valueLabel->setMinimumWidth(40);
    m_valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_valueSlider, &QSlider::valueChanged, this, [this](int value) {
        if (m_syncingColor)
            return;
        int hue = 0;
        int saturation = 0;
        int oldValue = 0;
        int alpha = 255;
        m_color.getHsv(&hue, &saturation, &oldValue, &alpha);
        const QColor next = QColor::fromHsv(hue < 0 ? 0 : hue, saturation, value, alpha);
        setCurrentColor(next, false);
    });
    valueRow->addWidget(valueTitle);
    valueRow->addWidget(m_valueSlider, 1);
    valueRow->addWidget(m_valueLabel);
    layout->addLayout(valueRow);

    auto *divider = new QFrame;
    divider->setObjectName(QStringLiteral("Divider"));
    divider->setFixedHeight(1);
    layout->addWidget(divider);

    auto *paletteHeader = new QHBoxLayout;
    paletteHeader->setSpacing(6);
    auto *paletteTitle = new QLabel(tr("本图颜色"));
    paletteTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_textureCountLabel = new QLabel(QStringLiteral("—"));
    m_textureCountLabel->setObjectName(QStringLiteral("Muted"));
    paletteHeader->addWidget(paletteTitle);
    paletteHeader->addWidget(m_textureCountLabel);
    m_mergeColorsButton = new QPushButton(tr("合并…"));
    m_mergeColorsButton->setObjectName(QStringLiteral("GhostButton"));
    m_mergeColorsButton->setToolTip(tr("手动勾选几种颜色并合并成一种"));
    m_mergeColorsButton->setFocusPolicy(Qt::NoFocus);
    connect(m_mergeColorsButton, &QPushButton::clicked, this,
            &TextureEditorPage::mergeColorsInteractive);
    paletteHeader->addWidget(m_mergeColorsButton);
    paletteHeader->addStretch(1);
    m_paletteScopeCheck = new QCheckBox(tr("只看当前帧"));
    m_paletteScopeCheck->setChecked(!m_paletteWholeTexture);
    m_paletteScopeCheck->setToolTip(
        tr("勾选后只统计当前帧；不勾选则统计整张材质（动画包含所有帧）"));
    connect(m_paletteScopeCheck, &QCheckBox::toggled, this,
            [this](bool) { applyPaletteScope(); });
    layout->addLayout(paletteHeader);

    m_textureEmptyLabel = new QLabel(tr("这张材质只有透明像素。"));
    m_textureEmptyLabel->setObjectName(QStringLiteral("PanelHint"));
    m_textureEmptyLabel->setVisible(false);
    layout->addWidget(m_textureEmptyLabel);

    m_textureGrid = new ColorSwatchGrid;
    m_textureGrid->setTooltipBuilder([this](const QColor &color, int count) {
        QString text = color.name(QColor::HexRgb).toUpper();
        if (color.alpha() != 255)
            text += tr(" · 透明度 %1%").arg(qRound(color.alphaF() * 100));
        if (count > 0) {
            text += tr(" · %1 像素").arg(count);
            if (m_textureOpaquePixels > 0) {
                text += tr("（%1%）")
                            .arg(qRound(100.0 * count / m_textureOpaquePixels));
            }
        }
        return text;
    });
    m_textureGrid->onPick = [this](const QColor &color) {
        setCurrentColor(color, true);
    };
    m_textureGrid->onContextMenu = [this](const QColor &color, const QPoint &pos) {
        showColorMenu(color, pos);
    };
    layout->addWidget(m_textureGrid);
    layout->addWidget(m_paletteScopeCheck);

    auto *defaultTitle = new QLabel(tr("默认 16 色"));
    defaultTitle->setObjectName(QStringLiteral("FieldLabel"));
    layout->addWidget(defaultTitle);

    m_paletteGrid = new ColorSwatchGrid;
    m_paletteGrid->setSwatches(minecraftPalette());
    m_paletteGrid->onPick = [this](const QColor &color) {
        setCurrentColor(color, true);
    };
    m_paletteGrid->onContextMenu = [this](const QColor &color, const QPoint &pos) {
        showColorMenu(color, pos);
    };
    layout->addWidget(m_paletteGrid);

    m_pinnedBox = new QWidget;
    auto *pinnedLayout = new QVBoxLayout(m_pinnedBox);
    pinnedLayout->setContentsMargins(0, 0, 0, 0);
    pinnedLayout->setSpacing(4);
    m_pinnedLabel = new QLabel(tr("收藏"));
    m_pinnedLabel->setObjectName(QStringLiteral("FieldLabel"));
    m_pinnedGrid = new ColorSwatchGrid;
    m_pinnedGrid->onPick = [this](const QColor &color) {
        setCurrentColor(color, true);
    };
    m_pinnedGrid->onContextMenu = [this](const QColor &color, const QPoint &pos) {
        showColorMenu(color, pos);
    };
    pinnedLayout->addWidget(m_pinnedLabel);
    pinnedLayout->addWidget(m_pinnedGrid);
    m_pinnedBox->setVisible(false);
    layout->addWidget(m_pinnedBox);

    m_recentBox = new QWidget;
    auto *recentLayout = new QVBoxLayout(m_recentBox);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->setSpacing(4);
    auto *recentTitle = new QLabel(tr("最近使用"));
    recentTitle->setObjectName(QStringLiteral("FieldLabel"));
    m_recentEmptyLabel = new QLabel(
        tr("还没用过颜色。选过的颜色会留在这里，关掉软件也不会丢。"));
    m_recentEmptyLabel->setObjectName(QStringLiteral("PanelHint"));
    m_recentEmptyLabel->setWordWrap(true);
    m_recentGrid = new ColorSwatchGrid;
    m_recentGrid->onPick = [this](const QColor &color) {
        setCurrentColor(color, true);
    };
    m_recentGrid->onContextMenu = [this](const QColor &color, const QPoint &pos) {
        showColorMenu(color, pos);
    };
    recentLayout->addWidget(recentTitle);
    recentLayout->addWidget(m_recentEmptyLabel);
    recentLayout->addWidget(m_recentGrid);
    layout->addWidget(m_recentBox);

    return section;
}

QWidget *TextureEditorPage::buildReferenceSection()
{
    auto *section = new QWidget;
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *divider = new QFrame;
    divider->setObjectName(QStringLiteral("Divider"));
    divider->setFixedHeight(1);
    layout->addWidget(divider);

    auto *title = new QLabel(tr("参考图"));
    title->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(title);

    m_referenceView = new ReferenceView;
    m_referenceView->setFixedHeight(200);
    m_referenceView->onColorPicked = [this](const QColor &color) {
        setCurrentColor(color, true);
    };
    m_referenceView->onViewChanged = [this] { updateReferenceUi(); };
    layout->addWidget(m_referenceView);

    auto *chooseRow = new QHBoxLayout;
    chooseRow->setSpacing(6);
    m_referenceChooseButton = new QPushButton(tr("选择参考图…"));
    m_referenceChooseButton->setObjectName(QStringLiteral("GhostButton"));
    m_referenceChooseButton->setToolTip(tr("参考图只用于对照，不会写进资源包"));
    auto *sourceMenu = new QMenu(m_referenceChooseButton);
    QAction *fromLibrary = sourceMenu->addAction(tr("从项目/参考材质…"));
    QAction *fromFile = sourceMenu->addAction(tr("从电脑图片…"));
    connect(fromLibrary, &QAction::triggered, this,
            &TextureEditorPage::chooseReferenceFromLibrary);
    connect(fromFile, &QAction::triggered, this,
            &TextureEditorPage::chooseReferenceFromFile);
    m_referenceChooseButton->setMenu(sourceMenu);

    m_referenceClearButton = new QPushButton(tr("清除"));
    m_referenceClearButton->setObjectName(QStringLiteral("GhostButton"));
    connect(m_referenceClearButton, &QPushButton::clicked, this,
            &TextureEditorPage::clearReference);
    chooseRow->addWidget(m_referenceChooseButton);
    chooseRow->addWidget(m_referenceClearButton);
    layout->addLayout(chooseRow);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(6);
    m_referenceFitButton = new QPushButton(tr("适应"));
    m_referenceFitButton->setObjectName(QStringLiteral("GhostButton"));
    m_referenceFitButton->setToolTip(tr("把参考图完整显示在预览框里"));
    connect(m_referenceFitButton, &QPushButton::clicked, this, [this] {
        if (m_referenceView)
            m_referenceView->fitToView();
    });
    m_referenceActualButton = new QPushButton(tr("1:1"));
    m_referenceActualButton->setObjectName(QStringLiteral("GhostButton"));
    m_referenceActualButton->setToolTip(tr("按参考图的原始像素大小显示"));
    connect(m_referenceActualButton, &QPushButton::clicked, this, [this] {
        if (m_referenceView)
            m_referenceView->resetZoom();
    });
    actionRow->addWidget(m_referenceFitButton);
    actionRow->addWidget(m_referenceActualButton);
    actionRow->addStretch(1);
    layout->addLayout(actionRow);

    m_referenceInfo = new QLabel;
    m_referenceInfo->setObjectName(QStringLiteral("PanelHint"));
    m_referenceInfo->setWordWrap(true);
    layout->addWidget(m_referenceInfo);

    updateReferenceUi();
    return section;
}

QWidget *TextureEditorPage::buildToolOptions()
{
    auto *section = new QWidget;
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);

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

    m_tileCombo = new QComboBox;
    m_tileCombo->addItem(tr("关闭"), 0);
    m_tileCombo->addItem(tr("全平铺"), 1);
    m_tileCombo->addItem(tr("水平平铺"), 2);
    m_tileCombo->addItem(tr("垂直平铺"), 3);
    m_tileCombo->setToolTip(
        tr("在画布上重复显示这张材质，方便检查接缝和重复纹理；编辑始终作用在中间那一块"));
    connect(m_tileCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        applyTileMode(m_tileCombo->currentData().toInt());
    });
    form->addRow(tr("平铺"), m_tileCombo);

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

    auto *reduceButton = new QPushButton(tr("减少颜色…"));
    reduceButton->setObjectName(QStringLiteral("GhostButton"));
    reduceButton->setToolTip(tr("把相近的颜色合并成一种，减少总色数"));
    connect(reduceButton, &QPushButton::clicked, this,
            &TextureEditorPage::openReduceColorsDialog);
    layout->addWidget(reduceButton);

    auto *clearMaskButton = new QPushButton(tr("清除调色蒙版"));
    clearMaskButton->setObjectName(QStringLiteral("GhostButton"));
    clearMaskButton->setToolTip(tr("清除套索/魔棒圈出的调色区域"));
    connect(clearMaskButton, &QPushButton::clicked, this, [this] {
        if (m_canvas)
            m_canvas->clearTintMask();
    });
    layout->addWidget(clearMaskButton);

    return section;
}

QWidget *TextureEditorPage::buildInfoSection()
{
    auto *section = new QWidget;
    auto *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);

    auto *divider = new QFrame;
    divider->setObjectName(QStringLiteral("Divider"));
    divider->setFixedHeight(1);
    layout->addWidget(divider);

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

    auto *sizeRow = new QWidget;
    auto *sizeLayout = new QHBoxLayout(sizeRow);
    sizeLayout->setContentsMargins(0, 0, 0, 0);
    sizeLayout->setSpacing(5);
    m_scaleUpButton = new QPushButton(tr("×2"));
    m_scaleUpButton->setObjectName(QStringLiteral("GhostButton"));
    m_scaleUpButton->setFixedWidth(40);
    m_scaleUpButton->setToolTip(tr("把尺寸放大一倍"));
    connect(m_scaleUpButton, &QPushButton::clicked, this,
            [this] { scaleTextureInteractive(2, 1); });
    m_scaleDownButton = new QPushButton(tr("÷2"));
    m_scaleDownButton->setObjectName(QStringLiteral("GhostButton"));
    m_scaleDownButton->setFixedWidth(40);
    m_scaleDownButton->setToolTip(tr("把尺寸缩小一半"));
    connect(m_scaleDownButton, &QPushButton::clicked, this,
            [this] { scaleTextureInteractive(1, 2); });
    sizeLayout->addWidget(m_infoSize, 1);
    sizeLayout->addWidget(m_scaleUpButton);
    sizeLayout->addWidget(m_scaleDownButton);

    infoForm->addRow(tr("尺寸"), sizeRow);
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

    return section;
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
    const bool bucket = tool == PixelCanvas::Tool::Bucket
                        || tool == PixelCanvas::Tool::MagicWand;
    const bool shape = tool == PixelCanvas::Tool::Rect || tool == PixelCanvas::Tool::Ellipse;

    if (m_brushSpin)
        m_brushSpin->setEnabled(brush);
    if (m_contiguousCheck)
        m_contiguousCheck->setEnabled(tool == PixelCanvas::Tool::Bucket);
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
    if (remember) {
        mcstudio::pushRecentColor(color);
        refreshColorLibrary();
    }
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

    if (m_valueSlider) {
        const QSignalBlocker valueBlocker(m_valueSlider);
        int hue = 0;
        int saturation = 0;
        int value = 255;
        int alphaValue = 255;
        m_color.getHsv(&hue, &saturation, &value, &alphaValue);
        m_valueSlider->setValue(value);
        if (m_valueLabel) {
            m_valueLabel->setText(
                QStringLiteral("%1%").arg(qRound(value * 100.0 / 255.0)));
        }
    }

    updateWheelFromColor();
    refreshSwatchSelections();
    m_syncingColor = false;
}

void TextureEditorPage::updateWheelFromColor()
{
    if (m_wheel)
        m_wheel->setColor(m_color);
}

void TextureEditorPage::refreshSwatchSelections()
{
    if (m_textureGrid)
        m_textureGrid->setSelectedColor(m_color);
    if (m_paletteGrid)
        m_paletteGrid->setSelectedColor(m_color);
    if (m_recentGrid)
        m_recentGrid->setSelectedColor(m_color);
    if (m_pinnedGrid)
        m_pinnedGrid->setSelectedColor(m_color);
}

void TextureEditorPage::refreshColorLibrary()
{
    if (!m_recentGrid || !m_pinnedGrid)
        return;

    const QVector<QColor> recent = mcstudio::recentColors();
    const QVector<QColor> pinned = mcstudio::pinnedColors();

    m_recentGrid->setSwatches(recent);
    m_recentEmptyLabel->setVisible(recent.isEmpty());
    m_recentGrid->setVisible(!recent.isEmpty());

    m_pinnedGrid->setSwatches(pinned);
    m_pinnedBox->setVisible(!pinned.isEmpty());
    if (m_pinnedLabel)
        m_pinnedLabel->setText(tr("收藏（%1）").arg(pinned.size()));

    refreshSwatchSelections();
}

void TextureEditorPage::refreshTexturePalette()
{
    if (!m_textureGrid)
        return;
    if (!m_document.isValid()) {
        m_textureGrid->setSwatches({});
        if (m_textureCountLabel)
            m_textureCountLabel->setText(QStringLiteral("—"));
        return;
    }

    const bool currentFrame = !m_paletteWholeTexture;
    const TextureDocument::PaletteStats stats =
        m_document.paletteStats(currentFrame, m_canvas ? m_canvas->frame() : 0);

    m_textureColorTotal = stats.colors.size();
    m_textureTransparent = stats.transparentPixels;
    m_textureOpaquePixels = stats.totalPixels - stats.transparentPixels;

    constexpr int kMaxSwatches = 128;
    const int shown = qMin(kMaxSwatches, stats.colors.size());
    m_textureColors.clear();
    m_textureCounts.clear();
    m_textureColors.reserve(shown);
    m_textureCounts.reserve(shown);
    for (int index = 0; index < shown; ++index) {
        m_textureColors.append(QColor::fromRgba(stats.colors.at(index).rgba));
        m_textureCounts.append(stats.colors.at(index).count);
    }

    m_textureGrid->setSwatches(m_textureColors, m_textureCounts);
    m_textureGrid->setVisible(!m_textureColors.isEmpty());
    if (m_textureEmptyLabel)
        m_textureEmptyLabel->setVisible(m_textureColors.isEmpty());

    if (m_textureCountLabel) {
        QString text = tr("%1 色").arg(m_textureColorTotal);
        if (m_textureColorTotal > shown)
            text += tr(" · 显示前 %1").arg(shown);
        if (m_textureTransparent > 0)
            text += tr(" · 透明 %1").arg(m_textureTransparent);
        m_textureCountLabel->setText(text);
    }

    if (m_paletteScopeCheck)
        m_paletteScopeCheck->setVisible(m_document.animated());

    refreshSwatchSelections();
}

void TextureEditorPage::mergeColorsInteractive()
{
    if (!m_document.isValid())
        return;

    const bool currentFrame = !m_paletteWholeTexture;
    const TextureDocument::PaletteStats stats =
        m_document.paletteStats(currentFrame, m_canvas->frame());
    if (stats.colors.size() < 2) {
        QMessageBox::information(this, tr("合并颜色"),
                                 tr("这张材质里没有两种以上的颜色可以合并。"));
        return;
    }

    MergeColorsDialog dialog(stats.colors, m_document.animated(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QVector<QRgb> selected = dialog.selectedColors();
    if (selected.size() < 2)
        return;

    int changed = 0;
    if (!m_document.mergeColors(selected, dialog.targetColor(),
                                dialog.applyAllFrames(), m_canvas->frame(),
                                &changed)) {
        return;
    }

    m_canvas->refresh();
    refreshAnimationEdit();
    updateDirtyUi();
    updateInfo();
    refreshTexturePalette();
}

void TextureEditorPage::applyPaletteScope(){
    if (!m_paletteScopeCheck)
        return;
    m_paletteWholeTexture = !m_paletteScopeCheck->isChecked();

    QSettings settings(QDir(AppPaths::dataRoot()).filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);
    settings.setValue(QStringLiteral("editor/paletteWhole"), m_paletteWholeTexture);

    refreshTexturePalette();
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
    if (m_templateButton)
        m_templateButton->setEnabled(valid);
    if (m_tintButton)
        m_tintButton->setEnabled(valid);
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
        updateScaleButtons();
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
    updateScaleButtons();
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
    refreshTexturePalette();
    updateScaleButtons();
}

void TextureEditorPage::updateTimeline()
{
    if (!m_timeline || !m_timelineLayout)
        return;

    clearLayout(m_timelineLayout);

    const bool valid = m_document.isValid();
    const bool animated = valid && m_document.animated() && m_document.frameCount() > 1;
    m_timeline->setVisible(valid && m_showTimeline);
    if (!valid || !m_showTimeline)
        return;
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
    if (!m_paletteWholeTexture)
        refreshTexturePalette();
}

bool TextureEditorPage::handleTextInputShortcut() const
{
    QWidget *focus = QApplication::focusWidget();
    return qobject_cast<QLineEdit *>(focus) || qobject_cast<QAbstractSpinBox *>(focus)
           || qobject_cast<QComboBox *>(focus)
           || qobject_cast<QPlainTextEdit *>(focus);
}

void TextureEditorPage::showColorMenu(const QColor &color, const QPoint &globalPos)
{
    if (!color.isValid())
        return;

    QMenu menu(this);
    QAction *pickAction = menu.addAction(tr("设为当前颜色"));
    const bool pinned = mcstudio::isPinnedColor(color);
    QAction *pinAction = menu.addAction(pinned ? tr("取消收藏") : tr("加入收藏"));
    menu.addSeparator();
    QAction *replaceAction = menu.addAction(tr("替换此颜色…"));
    replaceAction->setEnabled(color.alpha() > 0);
    QAction *copyAction = menu.addAction(tr("复制色值"));
    menu.addSeparator();
    QAction *clearAction = menu.addAction(tr("清空“最近使用”"));

    QAction *chosen = menu.exec(globalPos);
    if (!chosen)
        return;

    if (chosen == pickAction) {
        setCurrentColor(color, true);
    } else if (chosen == pinAction) {
        togglePinnedColor(color);
    } else if (chosen == replaceAction) {
        replaceColorInteractive(color);
    } else if (chosen == copyAction) {
        QApplication::clipboard()->setText(colorToText(color));
        QToolTip::showText(globalPos, tr("已复制 %1").arg(colorToText(color)), this);
    } else if (chosen == clearAction) {
        mcstudio::clearRecentColors();
        refreshColorLibrary();
    }
}

void TextureEditorPage::togglePinnedColor(const QColor &color)
{
    mcstudio::togglePinnedColor(color);
    refreshColorLibrary();
}

void TextureEditorPage::replaceColorInteractive(const QColor &color)
{
    if (!m_document.isValid() || color.alpha() == 0)
        return;

    ReplaceColorDialog dialog(color, m_document.animated(), this);
    dialog.setCountProvider([this, color](int tolerance, bool allFrames) {
        return m_document.countColor(color, tolerance, allFrames,
                                     m_canvas ? m_canvas->frame() : 0);
    });
    if (dialog.exec() != QDialog::Accepted)
        return;

    int changed = 0;
    if (!m_document.replaceColor(color, dialog.replacement(), dialog.tolerance(),
                                 dialog.allFrames(), m_canvas ? m_canvas->frame() : 0,
                                 &changed)) {
        QMessageBox::information(this, tr("替换颜色"),
                                 tr("没有找到可以替换的像素。"));
        return;
    }

    refreshAnimationEdit();
}

void TextureEditorPage::setReferenceImage(const QImage &image, const QString &name)
{
    if (image.isNull() || !m_referenceView)
        return;

    m_referenceName = name.isEmpty() ? tr("参考图") : name;
    if (m_referenceChooseButton)
        m_referenceChooseButton->setToolTip(m_referenceName);
    m_referenceView->setImage(image);
    m_referenceView->fitToView();
    updateReferenceUi();
}

void TextureEditorPage::chooseReferenceFromLibrary()
{
    if (!referencePathProvider)
        return;

    QStringList paths = referencePathProvider(true);
    paths.removeAll(m_path);
    if (paths.isEmpty()) {
        QMessageBox::information(this, tr("选择参考图"),
                                 tr("当前项目和参考实例里没有可用的材质。"));
        return;
    }

    TexturePickerDialog dialog(paths, tr("选择参考图"), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString path = dialog.selectedPath();
    if (path.isEmpty())
        return;

    QByteArray bytes;
    if (referenceBytesProvider)
        bytes = referenceBytesProvider(path);
    const QImage image = QImage::fromData(bytes);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("无法读取参考图"),
                             tr("这个材质不是可读取的图片：\n%1").arg(path));
        return;
    }

    setReferenceImage(image, QFileInfo(path).fileName());
    if (m_referenceChooseButton)
        m_referenceChooseButton->setToolTip(path);
}

void TextureEditorPage::chooseReferenceFromFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择参考图片"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.bmp *.webp);;所有文件 (*)"));
    if (path.isEmpty())
        return;

    const QImage image(path);
    if (image.isNull()) {
        QMessageBox::warning(this, tr("无法读取参考图"),
                             tr("这个文件不是可读取的图片：\n%1").arg(path));
        return;
    }

    setReferenceImage(image, QFileInfo(path).fileName());
    if (m_referenceChooseButton)
        m_referenceChooseButton->setToolTip(path);
}

void TextureEditorPage::clearReference()
{
    if (m_referenceView)
        m_referenceView->clearImage();
    m_referenceName.clear();
    if (m_referenceChooseButton)
        m_referenceChooseButton->setToolTip(tr("参考图只用于对照，不会写进资源包"));
    updateReferenceUi();
}

void TextureEditorPage::updateReferenceUi()
{
    if (!m_referenceInfo || !m_referenceView)
        return;

    const bool has = m_referenceView->hasImage();
    m_referenceFitButton->setEnabled(has);
    m_referenceActualButton->setEnabled(has);
    m_referenceClearButton->setEnabled(has);

    if (!has) {
        m_referenceInfo->setText(tr("参考图只用于对照，不会写进资源包。"));
        return;
    }

    const QSize size = m_referenceView->image().size();
    const QString name = m_referenceName.isEmpty() ? tr("参考图") : m_referenceName;
    m_referenceInfo->setText(
        tr("%1 · %2 × %3 · 缩放 %4%\n拖动平移 · 滚轮缩放 · 单击或右键取色 · 双击适应")
            .arg(name)
            .arg(size.width())
            .arg(size.height())
            .arg(m_referenceView->zoomPercent()));
}


void TextureEditorPage::openTemplateDialog()
{
    if (!m_document.isValid())
        return;

    auto paths = [this]() -> QStringList {
        return referencePathProvider ? referencePathProvider(true) : QStringList();
    };
    auto bytes = [this](const QString &path) -> QByteArray {
        return referenceBytesProvider ? referenceBytesProvider(path) : QByteArray();
    };

    TemplateDialog dialog(paths, bytes, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (dialog.equipmentRequested()) {
        openEquipmentDialog(dialog.selectedPackZip());
        return;
    }

    QImage image = dialog.selectedImage();
    if (image.isNull())
        return;
    const QSize frame = m_document.frameSize();
    if (image.size() != frame)
        image = image.scaled(frame, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    openTintDialogWithBase(image);
}

void TextureEditorPage::openEquipmentDialog(const QString &zipPath)
{
    if (!m_document.isValid() || zipPath.isEmpty())
        return;

    TemplateLibrary library;
    const QVector<TemplateEntry> assets = library.packAssets(zipPath);
    if (assets.size() < 2) {
        QMessageBox::information(this, tr("生成整套装备"),
                                 tr("这个模板包里没有多个 assets/ PNG。"));
        return;
    }

    EquipmentDialog dialog(m_project, zipPath, assets, this);
    dialog.onGenerated = [this](const QStringList &paths) {
        if (onFilesGenerated)
            onFilesGenerated(paths);
    };
    dialog.exec();
}

void TextureEditorPage::showEquipmentDialogForTesting()
{
    TemplateLibrary library;
    const QString zipPath = library.ensureBuiltinEquipmentPack();
    if (zipPath.isEmpty())
        return;
    const QVector<TemplateEntry> assets = library.packAssets(zipPath);
    EquipmentDialog dialog(m_project, zipPath, assets, this);
    dialog.onGenerated = [this](const QStringList &paths) {
        if (onFilesGenerated)
            onFilesGenerated(paths);
    };
    dialog.exec();
}

void TextureEditorPage::openTintDialog()
{
    if (!m_document.isValid())
        return;
    openTintDialogWithBase(QImage());
}

void TextureEditorPage::openReduceColorsDialog()
{
    if (!m_document.isValid())
        return;

    ReduceColorsDialog dialog(m_document.frame(m_canvas->frame()),
                              m_document.animated(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    applyColorReduction(dialog.targetColors(), dialog.applyAllFrames());
}

void TextureEditorPage::applyColorReduction(int targetColors, bool allFrames)
{
    if (!m_document.isValid())
        return;

    const bool animated = m_document.animated();
    const int first = (allFrames && animated) ? 0 : m_canvas->frame();
    const int last = (allFrames && animated) ? m_document.frameCount() - 1 : first;
    QVector<int> entries;
    QSet<int> visited;
    QVector<QImage> images;

    for (int entry = first; entry <= last; ++entry) {
        const int physical = m_document.playbackIndex(entry);
        if (visited.contains(physical))
            continue;
        visited.insert(physical);
        entries.append(entry);
        images.append(m_document.frame(entry));
    }
    if (images.isEmpty())
        return;

    const QVector<QRgb> palette = buildReducedPalette(images, targetColors);
    if (palette.isEmpty())
        return;

    bool changed = false;
    for (int entry : entries) {
        const QImage reduced = reduceColors(m_document.frame(entry), palette);
        if (reduced.isNull())
            continue;
        m_document.frameRef(entry) = reduced;
        changed = true;
    }
    if (!changed)
        return;

    m_document.commitEdit();
    m_canvas->refresh();
    refreshAnimationEdit();
    updateDirtyUi();
    updateInfo();
    refreshTexturePalette();
}

void TextureEditorPage::openTintDialogWithBase(const QImage &baseTemplate)
{
    if (!m_document.isValid())
        return;

    const QImage base = baseTemplate.isNull() ? m_document.frame(m_canvas->frame())
                                              : baseTemplate;
    if (base.isNull())
        return;

    TintDialog dialog(base, m_document.animated(), m_canvas->hasSelection(),
                      m_canvas->hasTintMask(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QImage mask;
    if (dialog.maskOnly() && m_canvas->hasTintMask()) {
        mask = m_canvas->tintMask();
    } else if (dialog.selectionOnly() && m_canvas->hasSelection()) {
        mask = QImage(m_document.frameSize(), QImage::Format_ARGB32);
        mask.fill(Qt::black);
        QPainter painter(&mask);
        painter.fillRect(m_canvas->selection(), Qt::white);
        painter.end();
    }

    const TintRecipe second = dialog.secondRecipe();
    applyTint(baseTemplate, dialog.recipe(), dialog.applyAllFrames(), mask,
              dialog.gradientEnabled() ? &second : nullptr, dialog.angleDegrees());
}

void TextureEditorPage::applyTint(const QImage &baseTemplate, const TintRecipe &recipe,
                                  bool allFrames, const QImage &mask,
                                  const TintRecipe *gradientRecipe,
                                  double angleDegrees)
{
    if (!m_document.isValid())
        return;

    const QSize frameSize = m_document.frameSize();
    const bool useMask = !mask.isNull() && mask.size() == frameSize;
    const bool animated = m_document.animated();
    const int first = (allFrames && animated) ? 0 : m_canvas->frame();
    const int last = (allFrames && animated) ? m_document.frameCount() - 1 : first;
    int changed = 0;
    QSet<int> visited;

    for (int entry = first; entry <= last; ++entry) {
        const int physical = m_document.playbackIndex(entry);
        if (visited.contains(physical))
            continue;
        visited.insert(physical);

        const QImage current = m_document.frame(entry);
        QImage base = baseTemplate.isNull() ? current : baseTemplate;
        if (base.isNull())
            continue;
        if (base.size() != frameSize)
            base = base.scaled(frameSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);

        if (!baseTemplate.isNull() && useMask) {
            const QImage maskImage = mask.format() == QImage::Format_ARGB32
                                         ? mask
                                         : mask.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < frameSize.height(); ++y) {
                QRgb *targetRow = reinterpret_cast<QRgb *>(base.scanLine(y));
                const QRgb *currentRow =
                    reinterpret_cast<const QRgb *>(current.constScanLine(y));
                const QRgb *maskRow =
                    reinterpret_cast<const QRgb *>(maskImage.constScanLine(y));
                for (int x = 0; x < frameSize.width(); ++x) {
                    if (qRed(maskRow[x]) < 128)
                        targetRow[x] = currentRow[x];
                }
            }
        }

        const QImage tinted =
            gradientRecipe
                ? recipe.applyGradient(base, *gradientRecipe, angleDegrees, mask)
                : recipe.apply(base, mask);
        if (tinted.isNull())
            continue;
        m_document.frameRef(entry) = tinted;
        ++changed;
    }

    if (changed == 0)
        return;

    m_document.commitEdit();
    m_canvas->refresh();
    refreshAnimationEdit();
    updateDirtyUi();
    updateInfo();
    refreshTexturePalette();
}

void TextureEditorPage::applyBuiltinTestTint()
{
    if (!m_document.isValid())
        return;
    const auto builtins = TemplateLibrary::builtinTemplates();
    for (const TemplateEntry &entry : builtins) {
        if (!entry.name.contains(QStringLiteral("锭")))
            continue;
        QImage image = entry.builtinImage.scaled(m_document.frameSize(),
                                                Qt::IgnoreAspectRatio,
                                                Qt::FastTransformation);
        applyTint(image, TintRecipe::preset(QStringLiteral("gold")), false,
                  QImage(), nullptr, 0.0);
        return;
    }
}

void TextureEditorPage::applyHalfMaskForTesting()
{
    if (!m_document.isValid() || !m_canvas)
        return;
    const QSize size = m_document.frameSize();
    QImage mask(size, QImage::Format_ARGB32);
    mask.fill(Qt::black);
    QPainter painter(&mask);
    painter.fillRect(QRect(size.width() / 2, 0, size.width() / 2, size.height()),
                     Qt::white);
    painter.end();
    m_canvas->setTintMask(mask);
}

void TextureEditorPage::applyHalfMaskTintForTesting()
{
    applyHalfMaskForTesting();
    if (!m_canvas || !m_canvas->hasTintMask())
        return;
    applyTint(QImage(), TintRecipe::preset(QStringLiteral("gold")), false,
              m_canvas->tintMask(), nullptr, 0.0);
}

bool TextureEditorPage::generateBuiltinEquipmentForTesting()
{
    TemplateLibrary library;
    const QString zipPath = library.ensureBuiltinEquipmentPack();
    const QVector<TemplateEntry> assets = library.packAssets(zipPath);
    if (assets.isEmpty())
        return false;

    QStringList generated;
    for (const TemplateEntry &entry : assets) {
        QImage image = library.load(entry);
        if (image.isNull())
            continue;
        QString path = entry.entryPath;
        path.replace(QStringLiteral("assets/minecraft/"),
                     QStringLiteral("assets/mcstudio_demo/"));
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer, "PNG"))
            continue;
        QString error;
        if (m_project.writeOutputFile(path, bytes, &error))
            generated.append(path);
    }
    if (generated.isEmpty())
        return false;
    if (onFilesGenerated)
        onFilesGenerated(generated);
    return true;
}

void TextureEditorPage::setWindowed(bool windowed)
{
    if (m_windowed == windowed)
        return;
    m_windowed = windowed;

    if (m_popOutButton) {
        m_popOutButton->setText(windowed ? tr("收回标签") : tr("独立窗口"));
        m_popOutButton->setToolTip(windowed
                                       ? tr("把材质放回主窗口的标签页")
                                       : tr("把这张材质放到单独的窗口里编辑"));
    }
    if (m_focusButton) {
        m_focusButton->setEnabled(!windowed);
        if (windowed && m_focusButton->isChecked())
            m_focusButton->setChecked(false);
    }
}

void TextureEditorPage::setFocusMode(bool enabled)
{
    if (m_focusMode == enabled)
        return;
    m_focusMode = enabled;
    if (m_focusButton) {
        const QSignalBlocker blocker(m_focusButton);
        m_focusButton->setChecked(enabled);
    }
}

void TextureEditorPage::restoreLayoutSettings()
{
    QSettings settings(QDir(AppPaths::dataRoot()).filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);
    m_panelOnRight = settings.value(QStringLiteral("editor/panelRight"), true).toBool();
    m_savedPanelWidth = settings.value(QStringLiteral("editor/panelWidth"), 300).toInt();
    if (m_savedPanelWidth < 280)
        m_savedPanelWidth = 300;
    m_showColorSection = settings.value(QStringLiteral("editor/showColor"), true).toBool();
    m_showToolsSection = settings.value(QStringLiteral("editor/showTools"), true).toBool();
    m_showInfoSection = settings.value(QStringLiteral("editor/showInfo"), true).toBool();
    m_showTimeline = settings.value(QStringLiteral("editor/showTimeline"), true).toBool();
    m_paletteWholeTexture =
        settings.value(QStringLiteral("editor/paletteWhole"), true).toBool();
}

void TextureEditorPage::saveLayoutSettings()
{
    QSettings settings(QDir(AppPaths::dataRoot()).filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);
    settings.setValue(QStringLiteral("editor/panelRight"), m_panelOnRight);
    settings.setValue(QStringLiteral("editor/panelWidth"), m_savedPanelWidth);
    settings.setValue(QStringLiteral("editor/showColor"), m_showColorSection);
    settings.setValue(QStringLiteral("editor/showTools"), m_showToolsSection);
    settings.setValue(QStringLiteral("editor/showInfo"), m_showInfoSection);
    settings.setValue(QStringLiteral("editor/showTimeline"), m_showTimeline);
    settings.setValue(QStringLiteral("editor/paletteWhole"), m_paletteWholeTexture);
}

void TextureEditorPage::setPanelOnRight(bool right)
{
    if (m_layoutApplied && m_sidePanelScroll && m_sidePanelScroll->width() > 150)
        m_savedPanelWidth = m_sidePanelScroll->width();
    m_panelOnRight = right;

    if (m_splitter && m_sidePanelScroll && m_toolColumn) {
        if (right) {
            m_splitter->addWidget(m_sidePanelScroll);
            m_splitter->setStretchFactor(1, 1);
            m_splitter->setStretchFactor(2, 0);
        } else {
            m_splitter->insertWidget(1, m_sidePanelScroll);
            m_splitter->setStretchFactor(1, 0);
            m_splitter->setStretchFactor(2, 1);
        }

        const int total = m_splitter->width();
        if (total > 400) {
            const int panel = qBound(280, m_savedPanelWidth, 560);
            const int canvas = qMax(320, total - 58 - panel);
            if (right)
                m_splitter->setSizes({58, canvas, panel});
            else
                m_splitter->setSizes({58, panel, canvas});
        }
    }

    if (m_panelRightAction)
        m_panelRightAction->setChecked(right);
    if (m_panelLeftAction)
        m_panelLeftAction->setChecked(!right);
    saveLayoutSettings();
}

void TextureEditorPage::applySectionVisibility()
{
    if (m_colorSection)
        m_colorSection->setVisible(m_showColorSection);
    if (m_toolSection)
        m_toolSection->setVisible(m_showToolsSection);
    if (m_infoSection)
        m_infoSection->setVisible(m_showInfoSection);

    if (m_showColorAction)
        m_showColorAction->setChecked(m_showColorSection);
    if (m_showToolsAction)
        m_showToolsAction->setChecked(m_showToolsSection);
    if (m_showInfoAction)
        m_showInfoAction->setChecked(m_showInfoSection);
    if (m_showTimelineAction)
        m_showTimelineAction->setChecked(m_showTimeline);

    updateTimeline();
}

void TextureEditorPage::resetLayout()
{
    m_showColorSection = true;
    m_showToolsSection = true;
    m_showInfoSection = true;
    m_showTimeline = true;
    m_savedPanelWidth = 300;
    m_layoutApplied = false;
    setPanelOnRight(true);
    applySectionVisibility();
    saveLayoutSettings();

    if (m_splitter && m_splitter->width() > 400) {
        const int total = m_splitter->width();
        m_splitter->setSizes({58, qMax(320, total - 58 - 300), 300});
        m_layoutApplied = true;
    }
}

void TextureEditorPage::applyTileMode(int mode)
{
    if (!m_canvas)
        return;

    const int bounded = qBound(0, mode, 3);
    m_canvas->setTileMode(static_cast<PixelCanvas::TileMode>(bounded));
    if (m_tileCombo) {
        const QSignalBlocker blocker(m_tileCombo);
        m_tileCombo->setCurrentIndex(bounded);
    }

    QSettings settings(QDir(AppPaths::dataRoot()).filePath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);
    settings.setValue(QStringLiteral("editor/tileMode"), bounded);
}

void TextureEditorPage::updateScaleButtons()
{
    if (!m_scaleUpButton || !m_scaleDownButton)
        return;

    const bool valid = m_document.isValid();
    const int side = m_document.frameSize().width();
    const int frames = qMax(1, m_document.physicalFrameCount());
    const bool canGrow = valid && side > 0 && side * 2 <= 4096
                         && qint64(side) * 2 * side * 2 * frames
                                <= 64LL * 1024 * 1024;
    const bool canShrink = valid && side > 1 && side % 2 == 0;

    m_scaleUpButton->setEnabled(canGrow);
    m_scaleDownButton->setEnabled(canShrink);
    m_scaleUpButton->setToolTip(
        canGrow ? tr("放大到 %1 × %1（像素按块复制，可撤销）").arg(side * 2)
                : tr("当前尺寸无法继续放大"));
    m_scaleDownButton->setToolTip(
        canShrink ? tr("缩小到 %1 × %1（会丢像素，可撤销）").arg(side / 2)
                  : (valid ? tr("当前尺寸不能被 2 整除，无法缩小") : tr("材质未加载")));
}

void TextureEditorPage::scaleTextureInteractive(int numerator, int denominator)
{
    if (!m_document.isValid())
        return;

    const int side = m_document.frameSize().width();
    const int target = side * numerator / denominator;
    const bool growing = numerator > denominator;
    const QString subject = m_document.animated() ? tr("每一帧") : tr("这张材质");

    const QString question =
        growing ? tr("把%1从 %2 × %2 放大到 %3 × %3？\n\n"
                     "像素会按方块复制，不会变模糊；可以用 Ctrl+Z 撤销。")
                      .arg(subject)
                      .arg(side)
                      .arg(target)
                : tr("把%1从 %2 × %2 缩小到 %3 × %3？\n\n"
                     "缩小会丢掉像素，只能用撤销找回。")
                      .arg(subject)
                      .arg(side)
                      .arg(target);

    const auto answer = QMessageBox::question(
        this, growing ? tr("放大尺寸") : tr("缩小尺寸"), question,
        QMessageBox::Yes | QMessageBox::No,
        growing ? QMessageBox::Yes : QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    QString error;
    if (!m_document.scaleTexture(numerator, denominator, &error)) {
        if (!error.isEmpty())
            QMessageBox::warning(this, tr("无法修改尺寸"), error);
        return;
    }

    m_canvas->refresh();
    m_canvas->fitToView();
    refreshAnimationEdit();
    updateScaleButtons();
}

void TextureEditorPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_layoutApplied)
        return;
    QTimer::singleShot(0, this, [this] {
        if (m_layoutApplied || !m_splitter || m_splitter->width() <= 400)
            return;
        m_layoutApplied = true;
        const int panel = qBound(280, m_savedPanelWidth, 560);
        const int total = m_splitter->width();
        m_splitter->setSizes({58, qMax(320, total - 58 - panel), panel});
    });
}

void TextureEditorPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_layoutApplied || !m_splitter || !m_sidePanelScroll)
        return;
    if (m_splitter->width() <= 400)
        return;

    m_layoutApplied = true;
    const int panel = qBound(280, m_savedPanelWidth, 560);
    const int total = m_splitter->width();
    m_splitter->setSizes({58, qMax(320, total - 58 - panel), panel});
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
    if (m_wheel)
        m_wheel->update();
    updateColorUi();
    refreshSwatchSelections();
    updateDirtyUi();
}

} // namespace mcstudio