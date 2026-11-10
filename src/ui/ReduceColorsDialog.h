#pragma once

#include <QDialog>
#include <QImage>

class QComboBox;
class QLabel;
class QSpinBox;

namespace mcstudio {

class ReduceColorsDialog : public QDialog
{
public:
    ReduceColorsDialog(const QImage &base, bool animated, QWidget *parent = nullptr);

    int targetColors() const;
    bool applyAllFrames() const;

private:
    void refreshPreview();
    QImage composePreview(const QImage &image) const;

    QImage m_base;
    bool m_animated = false;
    QSpinBox *m_targetSpin = nullptr;
    QComboBox *m_scopeCombo = nullptr;
    QLabel *m_originalPreview = nullptr;
    QLabel *m_resultPreview = nullptr;
    QLabel *m_info = nullptr;
};

} // namespace mcstudio
