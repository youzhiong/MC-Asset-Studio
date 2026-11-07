#pragma once

#include "core/GameSettings.h"

#include <QDialog>
#include <QVector>

class QComboBox;
class QDialogButtonBox;
class QCheckBox;
class QFrame;
class QLabel;
class QTabWidget;

namespace mcstudio {

class GameDirectoryPage;

// Preferences shell: appearance, game directory, reference fallback and cache.
class PreferencesDialog : public QDialog
{
public:
    explicit PreferencesDialog(QWidget *parent = nullptr);

    GameSettings settings() const;
    GameDirectoryPage *gamePage() const { return m_gamePage; }
    void setCurrentTab(int index);

private:
    void buildUi();
    QWidget *createAppearancePage();
    QWidget *createCachePage();
    QWidget *createReferencePage();
    void updateThemePreview();
    void updateStyleForTheme();
    void save();
    void refreshCacheSummary();
    void clearEnvironmentCache();
    void clearProjectAssociationCaches();

    GameDirectoryPage *m_gamePage = nullptr;
    QTabWidget *m_tabs = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QComboBox *m_styleCombo = nullptr;
    QFrame *m_themePreview = nullptr;
    QLabel *m_themeName = nullptr;
    QLabel *m_themeDescription = nullptr;
    QVector<QFrame *> m_themeSwatches;
    QLabel *m_cachePathLabel = nullptr;
    QLabel *m_cacheUsageLabel = nullptr;
    QCheckBox *m_fallbackModelsCheck = nullptr;
    QCheckBox *m_fallbackLangCheck = nullptr;
    QCheckBox *m_showReferenceCheck = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QString m_preferredStyle = QStringLiteral("Fusion");
};

} // namespace mcstudio
