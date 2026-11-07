#include "core/GameSettings.h"

#include "core/AppPaths.h"
#include "core/PathUtils.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>

namespace mcstudio {
namespace {

constexpr int kMaxRecentRoots = 10;

QString encodeList(const QStringList &values)
{
    QJsonArray array;
    for (const QString &value : values)
        array.append(value);
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

// Roots are stored as a JSON array so paths containing the INI list
// separators (comma / semicolon) still round-trip.
QStringList decodeList(const QVariant &value)
{
    const QString text = value.toString();
    if (!text.startsWith(QLatin1Char('[')))
        return value.toStringList();

    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8());
    QStringList result;
    if (document.isArray()) {
        for (const QJsonValue &entry : document.array()) {
            const QString path = entry.toString();
            if (!path.isEmpty())
                result.append(path);
        }
    }
    return result;
}

} // namespace

QString settingsFilePath()
{
    return QDir(AppPaths::dataRoot()).filePath(QStringLiteral("settings.ini"));
}

GameSettings loadGameSettings()
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);

    GameSettings result;
    result.roots = decodeList(settings.value(QStringLiteral("games/roots")));
    result.recentRoots = decodeList(settings.value(QStringLiteral("games/recent")));
    result.defaultInstanceKey =
        settings.value(QStringLiteral("games/defaultInstance")).toString();

    const QString mode =
        settings.value(QStringLiteral("reference/mode"), QStringLiteral("auto")).toString();
    result.referenceMode = mode == QLatin1String("default") ? QStringLiteral("default")
                                                           : QStringLiteral("auto");
    result.fallbackModels =
        settings.value(QStringLiteral("reference/fallbackModels"), true).toBool();
    result.fallbackLang =
        settings.value(QStringLiteral("reference/fallbackLang"), true).toBool();
    result.showReferenceTextures =
        settings.value(QStringLiteral("reference/showReferenceTextures"), false).toBool();
    result.showDisabledMods =
        settings.value(QStringLiteral("games/showDisabledMods"), false).toBool();
    result.theme = settings.value(QStringLiteral("appearance/theme"),
                                  QStringLiteral("win11light"))
                       .toString();
    result.widgetStyle = settings.value(QStringLiteral("appearance/style"),
                                       QStringLiteral("Fusion"))
                             .toString();

    // Drop duplicates and non-absolute leftovers from older settings files.
    QStringList cleaned;
    for (const QString &root : std::as_const(result.roots)) {
        const QString normalized = normalizedPath(root);
        if (normalized.isEmpty())
            continue;
        bool seen = false;
        for (const QString &existing : std::as_const(cleaned)) {
            if (samePath(existing, normalized)) {
                seen = true;
                break;
            }
        }
        if (!seen)
            cleaned.append(normalized);
    }
    result.roots = cleaned;

    QStringList recent;
    for (const QString &root : std::as_const(result.recentRoots)) {
        const QString normalized = normalizedPath(root);
        if (normalized.isEmpty() || recent.size() >= kMaxRecentRoots)
            continue;
        bool seen = false;
        for (const QString &existing : std::as_const(recent)) {
            if (samePath(existing, normalized)) {
                seen = true;
                break;
            }
        }
        if (!seen)
            recent.append(normalized);
    }
    result.recentRoots = recent;
    return result;
}

void saveGameSettings(const GameSettings &settings)
{
    QDir().mkpath(AppPaths::dataRoot());
    QSettings store(settingsFilePath(), QSettings::IniFormat);

    store.setValue(QStringLiteral("games/roots"), encodeList(settings.roots));
    store.setValue(QStringLiteral("games/recent"), encodeList(settings.recentRoots));
    store.setValue(QStringLiteral("games/defaultInstance"), settings.defaultInstanceKey);
    store.setValue(QStringLiteral("games/showDisabledMods"), settings.showDisabledMods);
    store.setValue(QStringLiteral("reference/mode"), settings.referenceMode);
    store.setValue(QStringLiteral("reference/fallbackModels"), settings.fallbackModels);
    store.setValue(QStringLiteral("reference/fallbackLang"), settings.fallbackLang);
    store.setValue(QStringLiteral("reference/showReferenceTextures"),
                   settings.showReferenceTextures);
    store.setValue(QStringLiteral("appearance/theme"), settings.theme);
    store.setValue(QStringLiteral("appearance/style"), settings.widgetStyle);
    store.sync();
}

void rememberGameRoot(GameSettings &settings, const QString &rootPath)
{
    const QString normalized = normalizedPath(rootPath);
    if (normalized.isEmpty())
        return;

    bool known = false;
    for (const QString &root : std::as_const(settings.roots)) {
        if (samePath(root, normalized)) {
            known = true;
            break;
        }
    }
    if (!known)
        settings.roots.append(normalized);

    QStringList recent;
    recent.append(normalized);
    for (const QString &root : std::as_const(settings.recentRoots)) {
        if (!samePath(root, normalized))
            recent.append(root);
        if (recent.size() >= kMaxRecentRoots)
            break;
    }
    settings.recentRoots = recent;
}

} // namespace mcstudio
