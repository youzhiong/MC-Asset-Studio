#pragma once

#include "core/GameEnvironment.h"
#include "core/GameScanTask.h"
#include "core/GameSettings.h"

#include <QWidget>
#include <functional>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTimer;
class QTreeWidget;

namespace mcstudio {

// Game directory page of the preferences dialog: manages multiple .minecraft
// roots, scans versions in the background and lists instances with their
// Minecraft version / loader / mod count / resource packs.
class GameDirectoryPage : public QWidget
{
public:
    explicit GameDirectoryPage(QWidget *parent = nullptr);
    ~GameDirectoryPage() override;

    void setSettings(const GameSettings &settings);
    GameSettings settings() const { return m_settings; }

    void startScan(bool forceRefresh);
    bool isScanning() const { return m_scanning; }
    const GameEnvironmentSnapshot &snapshot() const { return m_snapshot; }

    std::function<void()> onScanFinished;
    std::function<void()> onSettingsChanged;

private:
    void buildUi();
    void rebuildRootList(const QString &selectPath = QString());
    void addRootPath(const QString &path);
    void browseRoot();
    void removeSelectedRoot();
    void updateReferenceControls();
    void updateReferenceCombo();
    void updateReferenceModeFromButtons();
    void updateInstanceTable();
    bool instanceMatches(const InstanceInfo &instance, const QString &rootPath,
                         const QString &filter) const;
    QString selectedRootPath() const;
    void pollScan();
    void finishScan(const GameEnvironmentSnapshot &snapshot, bool ok, const QString &error);
    void updateSummary();
    void updateScanUi();
    void notifyChanged();

    GameSettings m_settings;
    GameEnvironmentSnapshot m_snapshot;
    GameScanTask m_scanTask;
    QTimer *m_pollTimer = nullptr;
    bool m_scanning = false;

    QListWidget *m_rootList = nullptr;
    QPushButton *m_addRootButton = nullptr;
    QPushButton *m_removeRootButton = nullptr;
    QPushButton *m_scanButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QLabel *m_scanStatusLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QTreeWidget *m_instanceTree = nullptr;
    QRadioButton *m_autoRadio = nullptr;
    QRadioButton *m_defaultRadio = nullptr;
    QComboBox *m_defaultCombo = nullptr;
    QLabel *m_referenceHint = nullptr;
};

} // namespace mcstudio
