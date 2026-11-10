#pragma once

#include "core/TextureDocument.h"

#include <QDialog>
#include <QVector>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace mcstudio {

class MergeColorsDialog : public QDialog
{
public:
    MergeColorsDialog(const QVector<TextureDocument::ColorCount> &colors,
                      bool animated, QWidget *parent = nullptr);

    QVector<QRgb> selectedColors() const;
    QRgb targetColor() const;
    bool applyAllFrames() const;

private:
    void updateState();
    QIcon swatchIcon(const QColor &color) const;

    QVector<TextureDocument::ColorCount> m_colors;
    bool m_animated = false;
    QListWidget *m_list = nullptr;
    QComboBox *m_targetCombo = nullptr;
    QComboBox *m_scopeCombo = nullptr;
    QLabel *m_info = nullptr;
    QPushButton *m_acceptButton = nullptr;
};

} // namespace mcstudio
