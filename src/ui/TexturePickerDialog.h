#pragma once

#include <QDialog>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace mcstudio {

// Small searchable list used to pick another texture (project or reference jar)
// for the reference-image overlay.
class TexturePickerDialog : public QDialog
{
public:
    TexturePickerDialog(const QStringList &paths, const QString &title,
                        QWidget *parent = nullptr);

    QString selectedPath() const;

private:
    void applyFilter(const QString &text);
    void updateSelection();

    QStringList m_paths;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_count = nullptr;
    QPushButton *m_acceptButton = nullptr;
};

} // namespace mcstudio
