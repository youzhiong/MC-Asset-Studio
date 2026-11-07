#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;

namespace mcstudio {

class OpenProjectDialog : public QDialog
{
public:
    explicit OpenProjectDialog(QWidget *parent = nullptr);

    QString selectedProjectDir() const { return m_projectDir; }
    QString browsedFolder() const { return m_browsedFolder; }

private:
    void populate(const QString &filter = QString());
    void updateSelection();

    QLineEdit *m_search = nullptr;
    QTreeWidget *m_tree = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_openButton = nullptr;
    QString m_projectDir;
    QString m_browsedFolder;
};

} // namespace mcstudio
