#pragma once

#include <QColor>
#include <QIcon>

namespace PixelIcons {

enum class Glyph {
    Pen,
    Eraser,
    Bucket,
    Dropper,
    Line,
    Rect,
    Ellipse,
    Select,
    Undo,
    Redo,
    Grid,
    Symmetry,
    Fit,
};

QIcon icon(Glyph glyph, const QColor &color = QColor(203, 213, 225), int size = 20);

} // namespace PixelIcons
