#pragma once

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace mcstudio {

class Project;

// One editable texture. Static textures own a single frame; animated textures
// own a list of physical square frames plus a playback list (mcmeta "frames").
// All edits are kept in memory and must be committed explicitly so a whole
// pencil stroke becomes a single undo step.
class TextureDocument
{
public:
    struct FrameRef {
        int physical = 0;
        int time = 1;
    };

    TextureDocument() = default;

    bool load(const Project &project, const QString &relativePath,
              QString *error = nullptr);
    bool save(const Project &project, QString *error = nullptr);

    QString relativePath() const { return m_relativePath; }
    bool isValid() const { return m_valid; }
    bool animated() const { return m_animated; }
    bool hasAnimationBlock() const { return m_hasAnimationBlock; }

    QSize frameSize() const;
    QSize textureSize() const;
    int frameCount() const { return m_playback.size(); }
    int physicalFrameCount() const { return m_frames.size(); }

    QImage frame(int entry) const;
    QImage &frameRef(int entry);
    int playbackIndex(int entry) const;
    int frameTime(int entry) const;

    int frametime() const { return m_frametime; }
    bool interpolate() const { return m_interpolate; }

    // Animation management. Each of these applies immediately and pushes one
    // undo step.
    bool createAnimation(int frameCount, int frametime, QString *error = nullptr);
    bool createAnimationFromStrip(QString *error = nullptr);
    bool deleteAnimation(QString *error = nullptr);
    bool addFrame(int afterEntry);
    bool removeFrame(int entry);
    bool moveFrame(int from, int to);
    void setFrameTime(int entry, int ticks);
    void setFrametime(int ticks);
    void setInterpolate(bool interpolate);

    // Palette helpers. Counts are sorted from most to least used and keep the
    // alpha channel, so half-transparent shades are listed separately.
    struct ColorCount {
        QRgb rgba = 0;
        int count = 0;
    };
    struct PaletteStats {
        QVector<ColorCount> colors;
        int transparentPixels = 0;
        int totalPixels = 0;
    };
    PaletteStats paletteStats(bool currentFrameOnly, int entry) const;
    int countColor(const QColor &target, int tolerance, bool allFrames, int entry) const;

    // Replace every matching pixel in the current frame or every physical frame.
    // The whole operation is one undo step. Returns false when nothing matched.
    bool replaceColor(const QColor &target, const QColor &replacement, int tolerance,
                      bool allFrames, int entry, int *changedPixels = nullptr);
    // Merge a set of exact RGBA colours into one colour. One undo step.
    bool mergeColors(const QVector<QRgb> &from, QRgb replacement, bool allFrames,
                     int entry, int *changedPixels = nullptr);

    // Nearest-neighbour resize of the whole texture (frame side × numerator /
    // denominator). Animated textures keep square frames and their frame count.
    bool scaleTexture(int numerator, int denominator, QString *error = nullptr);

    // History / dirty state.
    bool dirty() const;
    bool canUndo() const;
    bool canRedo() const;
    bool commitEdit();
    bool undo();
    bool redo();

    QString lastError() const { return m_lastError; }

private:
    struct Snapshot {
        QVector<QImage> frames;
        QVector<int> playback;
        QVector<int> times;
        QJsonObject mcmeta;
        bool animated = false;
        bool hasAnimationBlock = false;
        int frametime = 1;
        bool interpolate = false;
    };

    Snapshot snapshot() const;
    void restore(const Snapshot &state);
    bool statesEqual(const Snapshot &a, const Snapshot &b) const;
    void resetHistory();
    void trimHistory();
    QImage composeImage() const;
    QJsonObject buildMcmeta() const;
    int clampEntry(int entry) const;

    QString m_relativePath;
    QVector<QImage> m_frames;
    QVector<int> m_playback;
    QVector<int> m_times;
    QJsonObject m_mcmeta;
    bool m_animated = false;
    bool m_hasAnimationBlock = false;
    int m_frametime = 1;
    bool m_interpolate = false;
    bool m_valid = false;
    QString m_lastError;

    QVector<Snapshot> m_history;
    int m_historyIndex = -1;
    int m_savedIndex = -1;
};

} // namespace mcstudio
