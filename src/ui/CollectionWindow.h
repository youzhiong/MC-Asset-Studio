#pragma once

#include "core/Project.h"
#include "core/TextureAssociation.h"
#include "ui/CollectionCanvas.h"
#include "ui/TextureBoardView.h"

#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QVector>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QMenu;
class QPushButton;
class QSpinBox;
class QTabBar;
class QToolButton;

namespace mcstudio {

class AssetStack;
class ModCatalog;
class Project;
class CollectionWindow : public QMainWindow
{
public:
    CollectionWindow(const Project &project,
                     const QVector<TextureBoardEntry> &entries,
                     const QString &initialCategory = QStringLiteral("all"),
                     QWidget *parent = nullptr);
    void setGroupMode(const QString &mode);
    void setNamespaceFilter(const QString &namespaceId, const QString &label = QString());
    void setEntries(const QVector<TextureBoardEntry> &entries);
    void fitAll();
    void setReferenceContext(std::shared_ptr<const AssetStack> assets,
                             std::shared_ptr<const ModCatalog> catalog,
                             const QStringList &namespaces);
    void setAssociationResult(std::shared_ptr<const AssociationResult> association);

    std::function<void(const QString &)> onTextureSelected;

private:
    void changeEvent(QEvent *event) override;
    void rebuild();
    void buildFilterMenus();
    void updateFilterButtons();
    bool matchesFilters(const TextureBoardEntry &entry) const;
    void appendReferenceGroups(QVector<CollectionGroupData> &groups) const;
    bool ensureAssociationBuilt();
    int detectResolution(const Project &project) const;
    QVector<CollectionGroupData> buildTypeGroups() const;
    QVector<CollectionGroupData> buildAssociationGroups();
    QVector<CollectionGroupData> buildModGroups() const;
    QVector<CollectionGroupData> buildModAssociationGroups();
    QString modTitle(const QString &namespaceId, const QString &fallback) const;
    QString modelSourceLabel(const QString &modelSource) const;

    Project m_project;
    QString m_projectName;
    QVector<TextureBoardEntry> m_entries;
    AssociationResult m_association;
    bool m_associationBuilt = false;
    std::shared_ptr<const AssetStack> m_assets;
    std::shared_ptr<const ModCatalog> m_catalog;
    std::shared_ptr<const AssociationResult> m_sharedAssociation;
    bool m_associationPending = false;
    QStringList m_namespaces;
    QString m_categoryKey = QStringLiteral("all");
    QString m_namespaceFilter;
    QString m_namespaceLabel;
    QString m_groupMode = QStringLiteral("type");
    QSet<QString> m_selectedCategories;
    QSet<QString> m_selectedMods;
    bool m_filtersInitialized = false;

    CollectionCanvas *m_canvas = nullptr;
    QComboBox *m_groupCombo = nullptr;
    QToolButton *m_typeButton = nullptr;
    QToolButton *m_modButton = nullptr;
    QCheckBox *m_referenceCheck = nullptr;
    QSpinBox *m_widthSpin = nullptr;
    QFrame *m_settingsPanel = nullptr;
    QLabel *m_projectLabel = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_detectedLabel = nullptr;
};

} // namespace mcstudio
