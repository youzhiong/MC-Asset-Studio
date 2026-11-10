#pragma once

#include "core/Project.h"
#include "core/TemplateLibrary.h"
#include "core/TintRecipe.h"

#include <QDialog>
#include <QStringList>
#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;

namespace mcstudio {

class EquipmentDialog : public QDialog
{
public:
    EquipmentDialog(const Project &project, const QString &zipPath,
                    const QVector<TemplateEntry> &entries, QWidget *parent = nullptr);

    std::function<void(const QStringList &)> onGenerated;

private:
    void chooseRecipe();
    void generate();
    QString targetPath(const TemplateEntry &entry) const;

    Project m_project;
    QString m_zipPath;
    QVector<TemplateEntry> m_entries;
    TintRecipe m_recipe;
    bool m_hasRecipe = false;

    QListWidget *m_list = nullptr;
    QLineEdit *m_namespaceEdit = nullptr;
    QCheckBox *m_overwriteCheck = nullptr;
    QCheckBox *m_tintCheck = nullptr;
    QLabel *m_info = nullptr;
};

} // namespace mcstudio
