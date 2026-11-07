#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace mcstudio {

// Lists ZIP resource packs found in the configured .minecraft roots and
// instances so the user can import one without browsing the filesystem.
class ResourcePackImportDialog : public QDialog
{
public:
    explicit ResourcePackImportDialog(QWidget *parent = nullptr);

    QString selectedPath() const { return m_selectedPath; }

private:
    void loadPacks();
    void applyFilter();
    void updateSelection();

    QLineEdit *m_search = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_importButton = nullptr;
    QString m_selectedPath;
};

} // namespace mcstudio
