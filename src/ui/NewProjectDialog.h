#pragma once

#include "core/ProjectService.h"

#include <QByteArray>
#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace mcstudio {

class NewProjectDialog : public QDialog
{
public:
    explicit NewProjectDialog(QWidget *parent = nullptr);

    NewProjectRequest request() const { return m_request; }

private:
    void chooseIcon();
    void clearIcon();
    void updatePreview();
    void updateCreateEnabled();

    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_versionCombo = nullptr;
    QLineEdit *m_descriptionEdit = nullptr;
    QLabel *m_iconPreview = nullptr;
    QPushButton *m_chooseIconButton = nullptr;
    QPushButton *m_clearIconButton = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QPushButton *m_createButton = nullptr;
    NewProjectRequest m_request;
};

} // namespace mcstudio
