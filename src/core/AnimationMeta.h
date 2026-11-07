#pragma once

#include <QByteArray>
#include <QSize>
#include <QVector>

namespace mcstudio {

struct AnimationFrame {
    int index = 0;
    int time = 1;
};

class AnimationMeta
{
public:
    static AnimationMeta fromMcmeta(const QByteArray &mcmeta, const QSize &imageSize);

    bool valid = false;
    int frametime = 1;
    bool interpolate = false;
    QVector<AnimationFrame> frames;

    // Physical frame geometry. MC animations are vertical strips of square
    // frames; the playback list can repeat or reorder them, so frame count and
    // physical frame count are not necessarily equal.
    int frameHeight = 0;
    int physicalFrames = 0;

    int frameCount() const { return frames.size(); }
};

} // namespace mcstudio
