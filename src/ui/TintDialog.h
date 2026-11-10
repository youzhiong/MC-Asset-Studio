#pragma once

#include "core/TintRecipe.h"

#include <QDialog>
#include <QImage>

class QComboBox;
class QCheckBox;
class QLabel;
class QSlider;

namespace mcstudio {

class TintCurveEditor;
class TintRampEditor;

// Live preview + gradient ramp + luminance curve. Applying is handled by the
// editor page (one undo step for the whole frame, or every frame).
class TintDialog : public QDialog
{
public:
    TintDialog(const QImage &base, bool animated, bool hasSelection, bool hasMask,
               QWidget *parent = nullptr);

    TintRecipe recipe() const { return m_recipe; }
    TintRecipe secondRecipe() const { return m_secondRecipe; }
    bool applyAllFrames() const;
    bool selectionOnly() const;
    bool maskOnly() const;
    bool gradientEnabled() const;
    int angleDegrees() const;

private:
    void setRecipe(const TintRecipe &recipe, const QString &presetId = {});
    void refreshPreview();
    void syncPresetCombo();
    void saveRecipe();
    void loadRecipe();
    QImage composePreview(const QImage &image) const;

    QImage m_base;
    QImage m_previewBase;
    bool m_animated = false;
    bool m_hasSelection = false;
    bool m_hasMask = false;
    TintRecipe m_recipe;
    TintRecipe m_secondRecipe;
    bool m_syncing = false;

    QLabel *m_originalPreview = nullptr;
    QLabel *m_resultPreview = nullptr;
    QLabel *m_strengthLabel = nullptr;
    QSlider *m_strengthSlider = nullptr;
    QComboBox *m_presetCombo = nullptr;
    QComboBox *m_scopeCombo = nullptr;
    QComboBox *m_secondPreset = nullptr;
    QCheckBox *m_selectionCheck = nullptr;
    QCheckBox *m_maskCheck = nullptr;
    QCheckBox *m_gradientCheck = nullptr;
    QSlider *m_angleSlider = nullptr;
    QLabel *m_angleLabel = nullptr;
    TintRampEditor *m_ramp = nullptr;
    TintCurveEditor *m_curve = nullptr;
};

} // namespace mcstudio
