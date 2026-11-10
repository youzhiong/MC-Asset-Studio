#include "core/PaletteStore.h"

#include "core/AppPaths.h"

#include <QDir>
#include <QSettings>

namespace mcstudio {
namespace {

constexpr int kMaxRecent = 32;
constexpr int kMaxPinned = 24;

QVector<QColor> g_recent;
QVector<QColor> g_pinned;
bool g_loaded = false;

QString settingsPath()
{
    return QDir(AppPaths::dataRoot()).filePath(QStringLiteral("settings.ini"));
}

QString encode(const QVector<QColor> &colors)
{
    // A space-separated string avoids QSettings' comma-based QStringList
    // encoding, which would otherwise split the JSON-looking value.
    QStringList parts;
    parts.reserve(colors.size());
    for (const QColor &color : colors)
        parts.append(color.name(QColor::HexArgb));
    return parts.join(QLatin1Char(' '));
}

QVector<QColor> decode(const QVariant &value)
{
    QString text = value.toString();
    if (text.isEmpty())
        text = value.toStringList().join(QLatin1Char(' '));
    text.replace(QLatin1Char(','), QLatin1Char(' '));
    text.remove(QLatin1Char('['));
    text.remove(QLatin1Char(']'));
    text.remove(QLatin1Char('"'));

    QVector<QColor> result;
    const QStringList tokens = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    result.reserve(tokens.size());
    for (const QString &token : tokens) {
        const QColor color = QColor::fromString(token);
        if (color.isValid() && color.alpha() > 0)
            result.append(color);
    }
    return result;
}

void loadIfNeeded()
{
    if (g_loaded)
        return;
    g_loaded = true;

    QSettings settings(settingsPath(), QSettings::IniFormat);
    g_recent = decode(settings.value(QStringLiteral("editor/recentColors")));
    g_pinned = decode(settings.value(QStringLiteral("editor/pinnedColors")));
    if (g_recent.size() > kMaxRecent)
        g_recent.resize(kMaxRecent);
    if (g_pinned.size() > kMaxPinned)
        g_pinned.resize(kMaxPinned);
}

void save()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("editor/recentColors"), encode(g_recent));
    settings.setValue(QStringLiteral("editor/pinnedColors"), encode(g_pinned));
    settings.sync();
}

int indexOfColor(const QVector<QColor> &colors, const QColor &color)
{
    for (int index = 0; index < colors.size(); ++index) {
        if (colors.at(index) == color)
            return index;
    }
    return -1;
}

} // namespace

QVector<QColor> recentColors()
{
    loadIfNeeded();
    return g_recent;
}

QVector<QColor> pinnedColors()
{
    loadIfNeeded();
    return g_pinned;
}

bool isPinnedColor(const QColor &color)
{
    loadIfNeeded();
    return color.isValid() && indexOfColor(g_pinned, color) >= 0;
}

void pushRecentColor(const QColor &color)
{
    if (!color.isValid() || color.alpha() == 0)
        return;
    loadIfNeeded();

    const int existing = indexOfColor(g_recent, color);
    if (existing >= 0)
        g_recent.removeAt(existing);
    g_recent.prepend(color);
    while (g_recent.size() > kMaxRecent)
        g_recent.removeLast();
    save();
}

void togglePinnedColor(const QColor &color)
{
    if (!color.isValid() || color.alpha() == 0)
        return;
    loadIfNeeded();

    const int existing = indexOfColor(g_pinned, color);
    if (existing >= 0) {
        g_pinned.removeAt(existing);
    } else {
        g_pinned.prepend(color);
        while (g_pinned.size() > kMaxPinned)
            g_pinned.removeLast();
    }
    save();
}

void clearRecentColors()
{
    loadIfNeeded();
    g_recent.clear();
    save();
}

} // namespace mcstudio
