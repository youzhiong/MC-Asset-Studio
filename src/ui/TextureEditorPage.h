#pragma once

#include "core/Project.h"
#include "core/TextureDocument.h"
#include "ui/PixelCanvas.h"
#include "ui/PixelIcons.h"

#include <QColor>
#include <QVector>
#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLayout;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;
class QVBoxLayout;
class QHBoxLayout;

namespace mcstudio {

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

    std::function<void()> onSaved;
    std::function<void(bool)> onDirtyChanged;
    std::function<void()> onHistoryChanged;

private:
    void changeEvent(QEvent *event) override;
    void buildUi();
    QWidget *buildHeader();
    QWidget *buildToolColumn();
    QWidget *buildSidePanel();
    QWidget *buildTimeline();

    QToolButton *makeToolButton(PixelIcons::Glyph glyph, const QString &text,
                                const QString &tooltip, QWidget *parent);
    QPushButton *makePaletteButton(const QColor &color, QWidget *parent);

    void selectTool(PixelCanvas::Tool tool);
    void updateToolOptions();
    void setCurrentColor(const QColor &color, bool remember);
    void updateColorUi();
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
    void pushRecentColor(const QColor &color);
    void rebuildRecentColors();
    void clearLayout(QLayout *layout);
    bool handleTextInputShortcut() const;

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
    QGridLayout *m_paletteLayout = nullptr;
    QGridLayout *m_recentLayout = nullptr;
    QLabel *m_recentLabel = nullptr;
    QWidget *m_recentWidget = nullptr;

    QSpinBox *m_brushSpin = nullptr;
    QCheckBox *m_contiguousCheck = nullptr;
    QSpinBox *m_toleranceSpin = nullptr;
    QCheckBox *m_fillCheck = nullptr;
    QComboBox *m_symmetryCombo = nullptr;
    QCheckBox *m_gridCheck = nullptr;
    QCheckBox *m_onionCheck = nullptr;

    QSlider *m_zoomSlider = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QToolButton *m_undoButton = nullptr;
    QToolButton *m_redoButton = nullptr;
    QVector<QToolButton *> m_toolButtons;
    QVector<PixelIcons::Glyph> m_toolGlyphs;

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
    QVector<QColor> m_recentColors;
    bool m_syncingColor = false;
    bool m_lastDirty = false;
    int m_lastFrameCount = -1;
    bool m_lastAnimated = false;
};

} // namespace mcstudio
