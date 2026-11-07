#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace IconFactory {

enum class Glyph {
    Plus,
    Folder,
    Import,
    Save,
    Export,
    Search,
    Grid,
    List,
    Cube,
    Image,
    Layers,
    Info,
    Play,
    ChevronDown,
    Close
};

QPixmap glyphPixmap(Glyph glyph, const QColor &color = QColor(203, 213, 225), int size = 20);
QIcon icon(Glyph glyph, const QColor &color = QColor(203, 213, 225), int size = 20);
QIcon appIcon();

} // namespace IconFactory
