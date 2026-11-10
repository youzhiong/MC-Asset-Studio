#pragma once

#include <QColor>
#include <QVector>

namespace mcstudio {

// Editor-wide colour library shared by every texture tab and remembered across
// restarts. Recent colours are automatic; pinned colours are the user's own
// palette. Nothing here is written into a resource pack.
QVector<QColor> recentColors();
QVector<QColor> pinnedColors();

bool isPinnedColor(const QColor &color);
void pushRecentColor(const QColor &color);
void togglePinnedColor(const QColor &color);
void clearRecentColors();

} // namespace mcstudio
