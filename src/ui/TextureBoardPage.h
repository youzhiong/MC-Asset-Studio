#pragma once

#include "core/AssetStack.h"
#include "core/Project.h"
#include "ui/TextureBoardView.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <QWidget>
#include <functional>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;
class QTimer;

namespace mcstudio {

class ModCatalog;
class Project;
class TextureBoardPage : public QWidget
{
public:
    explicit TextureBoardPage(const Project &project, const AssetStack *referenceStack = nullptr,
                              bool showReference = false, QWidget *parent = nullptr);
    ~TextureBoardPage() override;

    std::function<void(const QString &)> onOpenTexture;
    std::function<void(const QString &)> onViewTexture;
    std::function<void(const QString &)> onTextureSelected;
    std::function<void(const QString &)> onImportReference;
    std::function<void(const QString &)> onImportReferenceNamespace;
    std::function<void()> onOpenCollection;
    void setCategoryFilter(const QString &key);
    void setNamespaceFilter(const QString &namespaceId, const QString &label = QString());
    void setSearchFilter(const QString &text);
    void setModCatalog(const std::shared_ptr<const ModCatalog> &catalog);
    void setReferenceStack(const AssetStack &stack);
    void setShowReferenceTextures(bool show);
    bool showReferenceTextures() const { return m_showReference; }
    void refreshPackEntries(const Project &project);
    void replaceTexture(const Project &project, const QString &path);
    QString categoryFilter() const { return m_categoryFilter; }
    QString namespaceFilter() const { return m_namespaceFilter; }
    void saveLayout();
    const QVector<TextureBoardEntry> &entries() const { return m_entries; }
    QVector<TextureBoardEntry> entriesWithReferences();

private:
    void changeEvent(QEvent *event) override;
    void updateThemeColors();
    TextureBoardEntry buildEntry(const QString &path, const QByteArray &bytes,
                                 const QByteArray &mcmeta, const QString &source,
                                 bool referenceOnly, const QString &referenceLabel = {});
    void loadEntries(const Project &project);
    void loadReferenceEntries();
    void rebuildEntries();
    void loadLayout(int &tileSize, bool &labels, bool &animation, bool &freeLayout,
                    int &zoom, QHash<QString, QPointF> &positions);
    void updateCountLabel();
    void scheduleSave();

    Project m_project;
    AssetStack m_referenceStack;
    bool m_hasReferenceStack = false;
    bool m_showReference = false;
    bool m_referenceLoaded = false;
    QString m_projectId;
    QString m_layoutPath;
    QString m_categoryFilter = QStringLiteral("all");
    QString m_namespaceFilter;
    QString m_modLabel;
    std::shared_ptr<const ModCatalog> m_modCatalog;
    TextureBoardView *m_view = nullptr;
    QLabel *m_zoomLabel = nullptr;
    QLabel *m_sizeLabel = nullptr;
    QLabel *m_countLabel = nullptr;
    QSlider *m_tileSlider = nullptr;
    QSlider *m_zoomSlider = nullptr;
    QCheckBox *m_labelsCheck = nullptr;
    QCheckBox *m_animationCheck = nullptr;
    QCheckBox *m_freeCheck = nullptr;
    QCheckBox *m_referenceCheck = nullptr;
    QComboBox *m_sourceCombo = nullptr;
    QLabel *m_packLegend = nullptr;
    QLabel *m_referenceLegend = nullptr;
    QTimer *m_saveTimer = nullptr;
    QVector<TextureBoardEntry> m_packEntries;
    QVector<TextureBoardEntry> m_referenceEntries;
    QVector<TextureBoardEntry> m_entries;
};

} // namespace mcstudio
