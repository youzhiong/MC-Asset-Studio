#pragma once

#include <QString>
#include <QStringList>

namespace mcstudio {

// Persistent user choices for the game environment and appearance. Stored in
// %LOCALAPPDATA%/MCStudio/settings.ini under [games] / [reference] /
// [appearance].
struct GameSettings {
    QStringList roots;        // .minecraft roots managed by the app
    QStringList recentRoots;  // most recently added first
    QString defaultInstanceKey;
    QString referenceMode = QStringLiteral("auto"); // auto | default
    bool fallbackModels = true;
    bool fallbackLang = true;
    bool showReferenceTextures = false;
    bool showDisabledMods = false;
    QString theme = QStringLiteral("win11light"); // appearance/theme
    QString widgetStyle = QStringLiteral("Fusion"); // appearance/style
};

QString settingsFilePath();
GameSettings loadGameSettings();
void saveGameSettings(const GameSettings &settings);
void rememberGameRoot(GameSettings &settings, const QString &rootPath);

} // namespace mcstudio
