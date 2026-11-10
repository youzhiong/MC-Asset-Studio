#pragma once

#include <QColor>
#include <QDialog>
#include <functional>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace mcstudio {

// Small dialog behind "替换此颜色…". It shows how many pixels would change and
// lets the user pick a replacement colour, a tolerance and (for animations)
// whether all frames or only the current one are affected.
class ReplaceColorDialog : public QDialog
{
public:
    ReplaceColorDialog(const QColor &target, bool animated, QWidget *parent = nullptr);

    QColor replacement() const { return m_replacement; }
    int tolerance() const;
    bool allFrames() const;

    // Returns how many pixels would change for the given tolerance / scope.
    void setCountProvider(std::function<int(int tolerance, bool allFrames)> provider);

private:
    void pickReplacement();
    void refreshCount();
    void updateSwatches();

    QColor m_target;
    QColor m_replacement;
    bool m_animated = false;

    QPushButton *m_targetSwatch = nullptr;
    QPushButton *m_newSwatch = nullptr;
    QLabel *m_targetHex = nullptr;
    QLabel *m_newHex = nullptr;
    QLabel *m_countLabel = nullptr;
    QSpinBox *m_toleranceSpin = nullptr;
    QComboBox *m_scopeCombo = nullptr;
    QPushButton *m_acceptButton = nullptr;
    std::function<int(int, bool)> m_countProvider;
};

} // namespace mcstudio
