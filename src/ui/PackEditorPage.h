#pragma once

#include "core/GameEnvironment.h"
#include "core/Project.h"

#include <QImage>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace mcstudio {

class PackEditorPage : public QWidget
{
public:
    PackEditorPage(const Project &project, const GameEnvironmentSnapshot &environment,
                   const QString &referenceInstanceKey, QWidget *parent = nullptr);

    std::function<void()> onSaved;
    std::function<void()> onOpenOverview;

    void setNamespaceSupported(const QString &namespaceId, bool supported);
    void saveNow();
    void setCoverForTesting(const QImage &cover);
    void setScopeAllForTesting(bool all);
    bool isDirty() const;

private:
    struct ModRow {
        QString name;
        QString modId;
        QStringList namespaces;
        QString instance;
        bool existing = false;
    };

    void rebuildMods();
    void rebuildTable();
    void updateSupportedFromItem(QTreeWidgetItem *item);
    void importCover();
    void updateCoverPreview();
    void saveChanges();
    QStringList existingNamespaces() const;
    const InstanceInfo *referenceInstance() const;

    Project m_project;
    GameEnvironmentSnapshot m_environment;
    QString m_referenceKey;

    QComboBox *m_scopeCombo = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QTreeWidget *m_modTree = nullptr;
    QLabel *m_coverPreview = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_saveButton = nullptr;

    QVector<ModRow> m_rows;
    QSet<QString> m_supported;
    QSet<QString> m_initialSupported;
    QImage m_pendingCover;
    bool m_coverChanged = false;
};

} // namespace mcstudio
