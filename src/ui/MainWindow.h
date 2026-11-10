#pragma once

#include "core/AssetStack.h"
#include "core/AssociationCache.h"
#include "core/GameEnvironment.h"
#include "core/GameSettings.h"
#include "core/ModCatalog.h"
#include "core/Project.h"
#include "core/ReferenceEnvironment.h"
#include "core/TextureAssociation.h"
#include "ui/IconFactory.h"

#include <QHash>
#include <QKeySequence>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <memory>

class QAction;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QProgressBar;
class QPushButton;
class QSplitter;
class QTabBar;
class QTimer;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace mcstudio {
class AssociationTask;
class CollectionWindow;
class DocumentArea;
class InspectorPanel;
class ModCatalogTask;
class PackEditorPage;
class ProjectOverviewWidget;
class TextureBoardPage;
class TextureEditorPage;
class TextureEditorWindow;
} // namespace mcstudio

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Import/export are public so the --import/--export smoke-test flags can
    // drive the same code path as the UI.
    bool importZip(const QString &zipPath, QString *error = nullptr);
    bool openProjectDirectory(const QString &projectDir, QString *error = nullptr);
    bool openTextureEditor(const QString &relativePath);
    void setTextureEditorReferenceForTesting(const QString &path);
    void popOutActiveTextureEditorForTesting();
    void returnActiveTextureEditorForTesting();
    void setTextureEditorFocusForTesting(bool enabled);
    void openTemplateDialogForTesting();
    void openTintDialogForTesting();
    void openReduceDialogForTesting();
    void applyBuiltinTintForTesting();
    void applyHalfMaskForTesting();
    void applyHalfMaskTintForTesting();
    void openEquipmentDialogForTesting();
    void openMergeDialogForTesting();
    bool generateEquipmentForTesting();
    bool saveActiveTextureEditorForTesting();
    QWidget *activeTextureEditorWindowForTesting() const;
    void openPackEditor();
    bool applyPackEditorNamespace(const QString &namespaceId, bool supported);
    bool applyPackEditorCover(const QString &imagePath);
    void showPackEditorAllForTesting();
    void fitCollectionForTesting();
    bool applyPackMetaForTesting(int packFormat, const QString &description);
    void setShowReferenceTextures(bool show);
    bool importReferencePath(const QString &path);
    bool importReferenceNamespaceForTesting(const QString &namespaceId);
    bool exportProject(const QString &targetZip, bool overwrite, QString *error = nullptr);
    void activateTab(const QString &key);
    void selectCategory(const QString &key);
    void selectMod(const QString &namespaceId);
    void selectReferenceInstance(const QString &instanceKey);
    void selectTexture(const QString &relativePath);
    void setSearchText(const QString &text);
    QString buildAssociationReport();
    QString buildDiagnosticsReport();
    bool waitForCatalogReady(int timeoutMs);
    bool waitForAssociationReady(int timeoutMs);
    void selectPackTab(int index);
    void showCollectionWindow();
    void setCollectionAssociationMode();
    void setCollectionGroupMode(const QString &mode);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createCentralArea();
    QWidget *createSidebar();
    QWidget *createCenter();
    QWidget *createInspector();
    void createStatusBar();
    void connectSignals();

    void importZipDialog();
    void importFromGameDirectory();
    void newProjectDialog();
    void openProjectDialog();
    void openProjectPath(const QString &path);
    void saveProject();
    void refreshRecentMenu();
    void rememberRecentProject(const QString &projectDir);
    QStringList recentProjects() const;
    void exportProjectDialog(bool overwrite);
    void openPreferencesDialog();
    void showAssociationDiagnostics();
    bool openProject(const QString &projectDir, QString *error = nullptr);
    void openOverviewTab();
    void openPackEditorTab();
    void openTextureBoard();
    void showTextureInInspector(const QString &path);
    mcstudio::TextureEditorPage *activeTextureEditor() const;
    void updateEditorActions();
    bool confirmCloseTextureEditors(const QString &context);
    void onTextureEdited(const QString &relativePath);
    void popOutTextureEditor(const QString &key);
    void returnTextureEditorToTab(const QString &key);
    bool requestCloseTextureEditorWindow(const QString &key);
    void closeAllTextureWindows();
    void updateTextureWindowTitle(const QString &key);
    void applyEditorFocusMode(bool enabled);
    QStringList referenceTexturePaths(bool includeReference) const;
    QByteArray readReferenceTexture(const QString &path) const;
    bool importReferenceTexture(const QString &path);
    bool importReferenceNamespace(const QString &namespaceId);
    void refreshTextureBoardAfterReferenceImport();
    void openCollectionWindow();
    void addPackTab(const mcstudio::Project &project);
    void activatePack(int index);
    void closePack(int index);
    void clearProject();
    void applyCategoryFilter();
    void updateCategoryCounts();
    void rebuildModCombo();
    void rebuildCategoryIndex();
    void applyModFilter();
    void rebuildReferenceCombo();
    void rebuildAssetStack();
    void onReferenceComboChanged();
    void startModCatalogForProject();
    void pollModCatalog();
    void applyModCatalog(const mcstudio::ModCatalog &catalog);
    void startAssociationBuild();
    void pollAssociation();
    void setTaskProgress(bool busy, int done = 0, int total = 0,
                         const QString &message = QString());
    void updateReferenceLabel();
    QString currentModLabel() const;
    const mcstudio::InstanceInfo *referenceInstanceInfo() const;
    const mcstudio::AssociationResult &associationResult();
    QString referenceInfoForTexture(const QString &relativePath);
    void addCategoryCount(const QString &namespaceId, const QString &key);
    void openTextTab(const QString &relativePath);
    void openImageTab(const QString &relativePath);
    void openFileTab(const QString &relativePath);
    void restoreProjectSession();
    void saveProjectSession();
    void updateProjectUi();
    void openDataFolder();
    void openProjectFolder();

    void updateBreadcrumb();
    void showNotImplemented(const QString &feature);
    void showAbout();
    void resetLayout();
    void restoreWindowState();
    void saveWindowState();

    QAction *makeAction(const QString &text, IconFactory::Glyph glyph,
                        const QKeySequence &shortcut = QKeySequence());

    QAction *m_newAction = nullptr;
    QAction *m_openAction = nullptr;
    QAction *m_importAction = nullptr;
    QAction *m_importGameAction = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_exportAction = nullptr;
    QAction *m_overwriteAction = nullptr;
    QAction *m_textureBoardAction = nullptr;
    QAction *m_collectionAction = nullptr;
    QAction *m_openDataAction = nullptr;
    QAction *m_exitAction = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QAction *m_findAction = nullptr;
    QAction *m_preferencesAction = nullptr;
    QAction *m_diagnosticsAction = nullptr;
    QAction *m_aboutAction = nullptr;
    QAction *m_aboutQtAction = nullptr;
    QAction *m_toggleSidebarAction = nullptr;
    QAction *m_toggleInspectorAction = nullptr;
    QAction *m_resetLayoutAction = nullptr;

    QTabBar *m_packBar = nullptr;
    QMenu *m_recentMenu = nullptr;
    QHash<QString, QString> m_packDirs;
    QString m_activePackId;
    bool m_switchingPack = false;
    QTreeWidget *m_categoryTree = nullptr;
    QHash<QString, QTreeWidgetItem *> m_categoryItems;
    QComboBox *m_modCombo = nullptr;
    QComboBox *m_referenceCombo = nullptr;
    QLabel *m_referenceLabel = nullptr;
    QLineEdit *m_sidebarSearch = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_breadcrumb = nullptr;
    mcstudio::DocumentArea *m_documentArea = nullptr;
    QPointer<mcstudio::TextureBoardPage> m_textureBoardPage;
    QPointer<mcstudio::PackEditorPage> m_packEditorPage;
    QPointer<mcstudio::CollectionWindow> m_collectionWindow;
    QHash<QString, QPointer<mcstudio::TextureEditorPage>> m_textureEditors;
    QHash<QString, QPointer<mcstudio::TextureEditorWindow>> m_textureWindows;
    bool m_editorFocusMode = false;
    bool m_focusSidebarWasVisible = true;
    bool m_focusInspectorWasVisible = true;
    bool m_shuttingDown = false;
    mcstudio::InspectorPanel *m_inspectorPanel = nullptr;
    QSplitter *m_splitter = nullptr;
    QWidget *m_sidebar = nullptr;
    QWidget *m_inspector = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_selectionLabel = nullptr;
    QWidget *m_countBadge = nullptr;
    QLabel *m_layerHint = nullptr;
    QPushButton *m_layerImportButton = nullptr;
    QLabel *m_projectStatusLabel = nullptr;
    QLabel *m_projectTargetLabel = nullptr;

    mcstudio::Project m_project;
    bool m_hasProject = false;

    mcstudio::GameSettings m_gameSettings;
    mcstudio::GameEnvironmentSnapshot m_environment;
    std::shared_ptr<mcstudio::ModCatalog> m_modCatalog;
    std::unique_ptr<mcstudio::ModCatalogTask> m_modTask;
    std::unique_ptr<mcstudio::AssociationTask> m_associationTask;
    QTimer *m_modPollTimer = nullptr;
    QTimer *m_associationPollTimer = nullptr;
    QProgressBar *m_taskProgress = nullptr;
    QStringList m_projectNamespaces;
    QString m_namespaceFilter;
    QString m_selectedTexturePath;
    mcstudio::ReferenceMatch m_referenceMatch;
    std::shared_ptr<mcstudio::AssetStack> m_assetStack;
    std::shared_ptr<mcstudio::AssociationResult> m_association;
    mcstudio::AssociationCacheKey m_associationCacheKey;
    QStringList m_stackDiagnostics;
    bool m_updatingReferenceCombo = false;
    QHash<QString, QHash<QString, int>> m_categoryCountsByNamespace;
    QHash<QString, int> m_categoryCountsAll;

    QSet<QString> m_noticeShown;
};
