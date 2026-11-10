#pragma once

#include "core/Project.h"
#include "core/TextureDocument.h"
#include "ui/PixelCanvas.h"
#include "ui/PixelIcons.h"

#include <QColor>
#include <QRect>
#include <QVector>
#include <QWidget>
#include <functional>

class QAction;
class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLayout;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QShowEvent;
class QResizeEvent;
class QSlider;
class QSpinBox;
class QSplitter;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QHBoxLayout;

namespace mcstudio {

class ColorSwatchGrid;
class ColorWheel;
class ReferenceView;
class TintRecipe;

class TextureEditorPage : public QWidget
{
public:
    TextureEditorPage(const Project &project, const QString &relativePath,
                      QWidget *parent = nullptr);
    ~TextureEditorPage() override;

    QString relativePath() const { return m_path; }
    bool isDirty() const;
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    bool saveDocument();
    void focusCanvas();
    void setWindowed(bool windowed);
    bool isWindowed() const { return m_windowed; }
    void setFocusMode(bool enabled);
    void setReferenceImage(const QImage &image, const QString &name = {});
    void showTemplateDialogForTesting() { openTemplateDialog(); }
    void showTintDialogForTesting() { openTintDialog(); }
    void showReduceDialogForTesting() { openReduceColorsDialog(); }
    void showEquipmentDialogForTesting();
    void showMergeDialogForTesting() { mergeColorsInteractive(); }
    void applyBuiltinTestTint();
    void applyHalfMaskForTesting();
    void applyHalfMaskTintForTesting();
    bool generateBuiltinEquipmentForTesting();

    std::function<void()> onSaved;
    std::function<void(bool)> onDirtyChanged;
    std::function<void()> onHistoryChanged;
    std::function<void()> onPopOut;
    std::function<void()> onReturnToTab;
    std::function<void(bool)> onFocusModeChanged;
    std::function<void(const QStringList &)> onFilesGenerated;
    // Reference image sources: project textures plus reference jars.
    std::function<QStringList(bool includeReference)> referencePathProvider;
    std::function<QByteArray(const QString &)> referenceBytesProvider;

private:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void buildUi();
    QWidget *buildHeader();
    QWidget *buildToolColumn();
    QWidget *buildSidePanel();
    QWidget *buildColorSection();
    QWidget *buildToolOptions();
    QWidget *buildInfoSection();
    QWidget *buildReferenceSection();
    QWidget *buildTimeline();
    void buildLayoutMenu(QToolButton *button);

    QToolButton *makeToolButton(PixelIcons::Glyph glyph, const QString &text,
                                const QString &tooltip, QWidget *parent);

    void selectTool(PixelCanvas::Tool tool);
    void updateToolOptions();
    void setCurrentColor(const QColor &color, bool remember);
    void updateColorUi();
    void updateWheelFromColor();
    void updateDirtyUi();
    void updateInfo();
    void updateTimeline();
    void updateAnimationPanel();
    void selectFrame(int entry);
    void createAnimationInteractive();
    void createAnimationFromStrip();
    void deleteAnimationInteractive();
    void addAnimationFrame();
    void removeAnimationFrame();
    void moveAnimationFrame(int delta);
    void togglePlayback();
    void advancePlayback();
    void stopPlayback();
    void refreshAnimationEdit();
    void clearLayout(QLayout *layout);
    bool handleTextInputShortcut() const;

    // Colour library (shared with every editor tab and persisted in settings).
    void refreshTexturePalette();
    void refreshColorLibrary();
    void refreshSwatchSelections();
    void showColorMenu(const QColor &color, const QPoint &globalPos);
    void replaceColorInteractive(const QColor &color);
    void togglePinnedColor(const QColor &color);
    void applyPaletteScope();
    void mergeColorsInteractive();

    // Layout customisation.
    void restoreLayoutSettings();
    void saveLayoutSettings();
    void setPanelOnRight(bool right);
    void applySectionVisibility();
    void resetLayout();

    // Texture size shortcuts.
    void updateScaleButtons();
    void scaleTextureInteractive(int numerator, int denominator);

    void applyTileMode(int mode);

    void chooseReferenceFromLibrary();
    void chooseReferenceFromFile();
    void clearReference();
    void updateReferenceUi();
    void openTemplateDialog();
    void openEquipmentDialog(const QString &zipPath);
    void openTintDialog();
    void openReduceColorsDialog();
    void openTintDialogWithBase(const QImage &baseTemplate);
    void applyTint(const QImage &baseTemplate, const TintRecipe &recipe,
                   bool allFrames, const QImage &mask,
                   const TintRecipe *gradientRecipe, double angleDegrees);
    void applyColorReduction(int targetColors, bool allFrames);

    Project m_project;
    QString m_path;
    TextureDocument m_document;

    PixelCanvas *m_canvas = nullptr;
    QLabel *m_sizeLabel = nullptr;
    QLabel *m_modeLabel = nullptr;
    QLabel *m_dirtyBadge = nullptr;
    QPushButton *m_saveButton = nullptr;
    QLabel *m_infoSize = nullptr;
    QLabel *m_infoFrame = nullptr;
    QLabel *m_infoZoom = nullptr;
    QLabel *m_infoCursor = nullptr;

    QPushButton *m_colorSwatch = nullptr;
    QLineEdit *m_hexEdit = nullptr;
    QSlider *m_alphaSlider = nullptr;
    QSpinBox *m_alphaSpin = nullptr;
    ColorWheel *m_wheel = nullptr;
    QSlider *m_valueSlider = nullptr;
    QLabel *m_valueLabel = nullptr;
    ColorSwatchGrid *m_textureGrid = nullptr;
    ColorSwatchGrid *m_paletteGrid = nullptr;
    ColorSwatchGrid *m_recentGrid = nullptr;
    ColorSwatchGrid *m_pinnedGrid = nullptr;
    QLabel *m_textureCountLabel = nullptr;
    QPushButton *m_mergeColorsButton = nullptr;
    QLabel *m_textureEmptyLabel = nullptr;
    QCheckBox *m_paletteScopeCheck = nullptr;
    QLabel *m_recentEmptyLabel = nullptr;
    QWidget *m_pinnedBox = nullptr;
    QLabel *m_pinnedLabel = nullptr;
    QWidget *m_recentBox = nullptr;
    int m_textureColorTotal = 0;
    int m_textureTransparent = 0;
    int m_textureOpaquePixels = 0;
    QVector<QColor> m_textureColors;
    QVector<int> m_textureCounts;

    QSpinBox *m_brushSpin = nullptr;
    QCheckBox *m_contiguousCheck = nullptr;
    QSpinBox *m_toleranceSpin = nullptr;
    QCheckBox *m_fillCheck = nullptr;
    QComboBox *m_symmetryCombo = nullptr;
    QComboBox *m_tileCombo = nullptr;
    QCheckBox *m_gridCheck = nullptr;
    QCheckBox *m_onionCheck = nullptr;

    QSlider *m_zoomSlider = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QToolButton *m_undoButton = nullptr;
    QToolButton *m_redoButton = nullptr;
    QToolButton *m_layoutButton = nullptr;
    QToolButton *m_popOutButton = nullptr;
    QToolButton *m_focusButton = nullptr;
    QToolButton *m_templateButton = nullptr;
    QToolButton *m_tintButton = nullptr;
    QVector<QToolButton *> m_toolButtons;
    QVector<PixelIcons::Glyph> m_toolGlyphs;

    QWidget *m_referenceSection = nullptr;
    ReferenceView *m_referenceView = nullptr;
    QLabel *m_referenceInfo = nullptr;
    QPushButton *m_referenceChooseButton = nullptr;
    QPushButton *m_referenceClearButton = nullptr;
    QPushButton *m_referenceFitButton = nullptr;
    QPushButton *m_referenceActualButton = nullptr;
    QString m_referenceName;

    QSplitter *m_splitter = nullptr;
    QWidget *m_toolColumn = nullptr;
    QScrollArea *m_sidePanelScroll = nullptr;
    QWidget *m_colorSection = nullptr;
    QWidget *m_toolSection = nullptr;
    QWidget *m_infoSection = nullptr;
    QAction *m_panelRightAction = nullptr;
    QAction *m_panelLeftAction = nullptr;
    QAction *m_showColorAction = nullptr;
    QAction *m_showToolsAction = nullptr;
    QAction *m_showInfoAction = nullptr;
    QAction *m_showTimelineAction = nullptr;

    QPushButton *m_scaleUpButton = nullptr;
    QPushButton *m_scaleDownButton = nullptr;

    QWidget *m_staticAnimationBox = nullptr;
    QWidget *m_animatedAnimationBox = nullptr;
    QPushButton *m_createAnimationButton = nullptr;
    QPushButton *m_createFromStripButton = nullptr;
    QPushButton *m_deleteAnimationButton = nullptr;
    QPushButton *m_addFrameButton = nullptr;
    QPushButton *m_removeFrameButton = nullptr;
    QPushButton *m_moveLeftButton = nullptr;
    QPushButton *m_moveRightButton = nullptr;
    QPushButton *m_playButton = nullptr;
    QSpinBox *m_frameTimeSpin = nullptr;
    QSpinBox *m_frametimeSpin = nullptr;
    QCheckBox *m_interpolateCheck = nullptr;
    QLabel *m_animationInfo = nullptr;
    QTimer *m_playTimer = nullptr;
    bool m_playing = false;
    bool m_syncingAnimation = false;

    QWidget *m_timeline = nullptr;
    QWidget *m_timelineTop = nullptr;
    QHBoxLayout *m_timelineLayout = nullptr;
    QScrollArea *m_timelineScroll = nullptr;

    QColor m_color = QColor(QStringLiteral("#f9fffe"));
    bool m_syncingColor = false;
    bool m_showTimeline = true;
    bool m_showColorSection = true;
    bool m_showToolsSection = true;
    bool m_showInfoSection = true;
    bool m_windowed = false;
    bool m_focusMode = false;
    bool m_panelOnRight = true;
    bool m_paletteWholeTexture = true;
    int m_savedPanelWidth = 300;
    bool m_layoutApplied = false;
    bool m_lastDirty = false;
    int m_lastFrameCount = -1;
    bool m_lastAnimated = false;
};

} // namespace mcstudio
