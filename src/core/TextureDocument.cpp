#include "core/TextureDocument.h"

#include "core/Project.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPainter>

#include <algorithm>

namespace mcstudio {
namespace {

constexpr int kMaxHistoryStates = 80;
constexpr qint64 kMaxHistoryBytes = 128LL * 1024 * 1024;

QString normalizedRelative(const QString &path)
{
    QString result = QDir::fromNativeSeparators(path);
    while (result.startsWith(QLatin1Char('/')))
        result.remove(0, 1);
    return result;
}

} // namespace

bool TextureDocument::load(const Project &project, const QString &relativePath,
                           QString *error)
{
    m_lastError.clear();
    m_valid = false;
    m_relativePath = normalizedRelative(relativePath);
    m_frames.clear();
    m_playback.clear();
    m_times.clear();
    m_mcmeta = QJsonObject();
    m_animated = false;
    m_hasAnimationBlock = false;
    m_frametime = 1;
    m_interpolate = false;
    m_history.clear();
    m_historyIndex = -1;
    m_savedIndex = -1;

    if (m_relativePath.isEmpty()) {
        m_lastError = QStringLiteral("材质路径为空。");
        if (error)
            *error = m_lastError;
        return false;
    }

    const QByteArray bytes = project.readBytes(m_relativePath);
    if (bytes.isEmpty()) {
        m_lastError = QStringLiteral("找不到材质：%1").arg(m_relativePath);
        if (error)
            *error = m_lastError;
        return false;
    }

    QImage image = QImage::fromData(bytes);
    if (image.isNull()) {
        m_lastError = QStringLiteral("无法解码 PNG：%1").arg(m_relativePath);
        if (error)
            *error = m_lastError;
        return false;
    }
    if (image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);

    const QByteArray metaBytes = project.readBytes(m_relativePath + QStringLiteral(".mcmeta"));
    if (!metaBytes.isEmpty()) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(metaBytes, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject())
            m_mcmeta = document.object();
        else
            m_mcmeta = QJsonObject();
    }

    if (m_mcmeta.value(QStringLiteral("animation")).isObject()) {
        m_hasAnimationBlock = true;
        const QJsonObject animation = m_mcmeta.value(QStringLiteral("animation")).toObject();
        m_frametime = qBound(1, animation.value(QStringLiteral("frametime")).toInt(1), 1000);
        m_interpolate = animation.value(QStringLiteral("interpolate")).toBool(false);

        const int width = image.width();
        const int height = image.height();
        const int physical = (width > 0 && height % width == 0) ? height / width : 1;

        if (physical > 1) {
            m_frames.reserve(physical);
            for (int index = 0; index < physical; ++index) {
                m_frames.append(image.copy(0, index * width, width, width));
            }

            const QJsonArray frameValues =
                animation.value(QStringLiteral("frames")).toArray();
            for (const QJsonValue &value : frameValues) {
                int index = 0;
                int time = m_frametime;
                if (value.isDouble()) {
                    index = value.toInt();
                } else if (value.isObject()) {
                    const QJsonObject object = value.toObject();
                    index = object.value(QStringLiteral("index")).toInt();
                    time = object.value(QStringLiteral("time")).toInt(m_frametime);
                } else {
                    continue;
                }
                index = qBound(0, index, physical - 1);
                m_playback.append(index);
                m_times.append(qBound(1, time, 1000));
            }

            if (m_playback.isEmpty()) {
                for (int index = 0; index < physical; ++index) {
                    m_playback.append(index);
                    m_times.append(m_frametime);
                }
            }
            m_animated = true;
        }
    }

    if (m_frames.isEmpty()) {
        m_frames.append(image);
        m_playback.append(0);
        m_times.append(m_frametime);
        m_animated = false;
    }

    m_valid = true;
    resetHistory();
    m_savedIndex = m_historyIndex;
    return true;
}

QSize TextureDocument::frameSize() const
{
    return m_frames.isEmpty() ? QSize() : m_frames.first().size();
}

QSize TextureDocument::textureSize() const
{
    if (!m_animated)
        return frameSize();
    const QSize frame = frameSize();
    return QSize(frame.width(), frame.height() * m_frames.size());
}

QImage TextureDocument::frame(int entry) const
{
    if (m_frames.isEmpty())
        return {};
    const int bounded = clampEntry(entry);
    if (bounded < 0 || bounded >= m_playback.size())
        return {};
    return m_frames.value(m_playback.at(bounded));
}

QImage &TextureDocument::frameRef(int entry)
{
    static QImage empty;
    if (m_frames.isEmpty() || m_playback.isEmpty())
        return empty;
    const int bounded = clampEntry(entry);
    const int physical = m_playback.value(qMax(0, bounded), 0);
    return m_frames[qBound(0, physical, qMax(0, m_frames.size() - 1))];
}

int TextureDocument::playbackIndex(int entry) const
{
    return m_playback.value(entry, 0);
}

int TextureDocument::frameTime(int entry) const
{
    return m_times.value(entry, m_frametime);
}

int TextureDocument::clampEntry(int entry) const
{
    if (m_playback.isEmpty())
        return -1;
    return qBound(0, entry, m_playback.size() - 1);
}

bool TextureDocument::createAnimation(int frameCount, int frametime, QString *error)
{
    if (!m_valid || m_frames.isEmpty()) {
        if (error)
            *error = QStringLiteral("材质尚未加载。");
        return false;
    }
    if (m_animated) {
        if (error)
            *error = QStringLiteral("这张材质已经是动画，请直接编辑或删除动画。");
        return false;
    }

    const QImage base = m_frames.first();
    if (base.width() != base.height()) {
        if (error) {
            *error = QStringLiteral("动画要求正方形帧，当前贴图是 %1×%2。\n"
                                    "请先把贴图调整为正方形，或对已有垂直帧条使用“按帧条拆分”。")
                         .arg(base.width())
                         .arg(base.height());
        }
        return false;
    }
    if (frameCount < 2 || frameCount > 256) {
        if (error)
            *error = QStringLiteral("帧数必须在 2–256 之间。");
        return false;
    }

    m_frametime = qBound(1, frametime, 1000);
    m_frames.clear();
    m_playback.clear();
    m_times.clear();
    for (int index = 0; index < frameCount; ++index) {
        m_frames.append(base.copy());
        m_playback.append(index);
        m_times.append(m_frametime);
    }
    m_animated = true;
    m_hasAnimationBlock = true;
    m_interpolate = false;
    commitEdit();
    return true;
}

bool TextureDocument::createAnimationFromStrip(QString *error)
{
    if (!m_valid || m_frames.isEmpty()) {
        if (error)
            *error = QStringLiteral("材质尚未加载。");
        return false;
    }
    if (m_animated) {
        if (error)
            *error = QStringLiteral("这张材质已经是动画。");
        return false;
    }

    const QImage strip = m_frames.first();
    const int width = strip.width();
    const int height = strip.height();
    if (width <= 0 || height % width != 0 || height / width < 2) {
        if (error)
            *error = QStringLiteral("当前贴图不是可拆分的垂直帧条（%1×%2）。")
                         .arg(width)
                         .arg(height);
        return false;
    }

    const int physical = height / width;
    m_frames.clear();
    m_playback.clear();
    m_times.clear();
    for (int index = 0; index < physical; ++index) {
        m_frames.append(strip.copy(0, index * width, width, width));
        m_playback.append(index);
        m_times.append(m_frametime);
    }
    m_animated = true;
    m_hasAnimationBlock = true;
    commitEdit();
    return true;
}

bool TextureDocument::deleteAnimation(QString *error)
{
    if (!m_animated) {
        if (error)
            *error = QStringLiteral("这张材质不是动画。");
        return false;
    }

    const QImage first = frame(0);
    if (first.isNull()) {
        if (error)
            *error = QStringLiteral("无法读取第一帧。");
        return false;
    }

    m_frames.clear();
    m_frames.append(first);
    m_playback.clear();
    m_playback.append(0);
    m_times.clear();
    m_times.append(m_frametime);
    m_animated = false;
    commitEdit();
    return true;
}

bool TextureDocument::addFrame(int afterEntry)
{
    if (!m_animated || m_frames.isEmpty())
        return false;
    const int entry = clampEntry(afterEntry);
    if (entry < 0)
        return false;

    const int physical = m_playback.at(entry);
    m_frames.append(m_frames.at(physical).copy());
    const int newPhysical = m_frames.size() - 1;
    m_playback.insert(entry + 1, newPhysical);
    m_times.insert(entry + 1, m_times.value(entry, m_frametime));
    commitEdit();
    return true;
}

bool TextureDocument::removeFrame(int entry)
{
    if (!m_animated || m_playback.size() <= 2)
        return false;
    const int bounded = clampEntry(entry);
    if (bounded < 0)
        return false;

    const int physical = m_playback.takeAt(bounded);
    m_times.removeAt(bounded);

    if (!m_playback.contains(physical)) {
        m_frames.removeAt(physical);
        for (int &index : m_playback) {
            if (index > physical)
                --index;
        }
    }
    commitEdit();
    return true;
}

bool TextureDocument::moveFrame(int from, int to)
{
    if (!m_animated)
        return false;
    if (from < 0 || from >= m_playback.size() || to < 0 || to >= m_playback.size()
        || from == to) {
        return false;
    }
    m_playback.move(from, to);
    m_times.move(from, to);
    commitEdit();
    return true;
}

void TextureDocument::setFrameTime(int entry, int ticks)
{
    const int bounded = clampEntry(entry);
    if (bounded < 0)
        return;
    const int value = qBound(1, ticks, 1000);
    if (m_times.value(bounded) == value)
        return;
    m_times[bounded] = value;
    commitEdit();
}

void TextureDocument::setFrametime(int ticks)
{
    const int value = qBound(1, ticks, 1000);
    if (m_frametime == value)
        return;
    const int previous = m_frametime;
    m_frametime = value;
    for (int &time : m_times) {
        if (time == previous)
            time = value;
    }
    commitEdit();
}

void TextureDocument::setInterpolate(bool interpolate)
{
    if (m_interpolate == interpolate)
        return;
    m_interpolate = interpolate;
    commitEdit();
}

bool TextureDocument::save(const Project &project, QString *error)
{
    if (!m_valid || m_frames.isEmpty()) {
        if (error)
            *error = QStringLiteral("材质尚未加载。");
        return false;
    }

    // Capture any in-place stroke that was not explicitly committed yet.
    commitEdit();

    const QImage output = composeImage();
    if (output.isNull()) {
        if (error)
            *error = QStringLiteral("无法生成输出图片。");
        return false;
    }

    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !output.save(&buffer, "PNG")) {
        if (error)
            *error = QStringLiteral("PNG 编码失败。");
        return false;
    }

    const QJsonObject meta = buildMcmeta();
    const QString metaPath = m_relativePath + QStringLiteral(".mcmeta");
    const bool writeMeta = m_animated || !meta.isEmpty() || project.exists(metaPath);

    const QByteArray previousPng = project.readBytes(m_relativePath);
    if (!project.writeOutputFile(m_relativePath, png, error))
        return false;

    if (writeMeta) {
        const QByteArray metaBytes = QJsonDocument(meta).toJson(QJsonDocument::Indented);
        if (!project.writeOutputFile(metaPath, metaBytes, error)) {
            // Keep the previous PNG if the metadata could not be committed.
            if (previousPng.isEmpty())
                project.removeOutputFile(m_relativePath);
            else
                project.writeOutputFile(m_relativePath, previousPng, nullptr);
            return false;
        }
    } else {
        project.removeOutputFile(metaPath);
    }

    m_mcmeta = meta;
    m_hasAnimationBlock = m_animated;
    m_savedIndex = m_historyIndex;
    return true;
}

QImage TextureDocument::composeImage() const
{
    if (m_frames.isEmpty())
        return {};
    if (!m_animated)
        return frame(0);

    const QSize size = frameSize();
    if (size.isEmpty())
        return {};
    QImage result(size.width(), size.height() * m_frames.size(),
                  QImage::Format_ARGB32);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (int index = 0; index < m_frames.size(); ++index)
        painter.drawImage(0, index * size.height(), m_frames.at(index));
    painter.end();
    return result;
}

QJsonObject TextureDocument::buildMcmeta() const
{
    QJsonObject root = m_mcmeta;

    if (m_animated) {
        QJsonObject animation = root.value(QStringLiteral("animation")).toObject();
        animation.insert(QStringLiteral("frametime"), m_frametime);
        if (m_interpolate)
            animation.insert(QStringLiteral("interpolate"), true);
        else
            animation.remove(QStringLiteral("interpolate"));

        bool identity = m_playback.size() == m_frames.size();
        if (identity) {
            for (int index = 0; index < m_playback.size(); ++index) {
                if (m_playback.at(index) != index || m_times.value(index) != m_frametime) {
                    identity = false;
                    break;
                }
            }
        }

        if (identity) {
            animation.remove(QStringLiteral("frames"));
        } else {
            QJsonArray frames;
            for (int index = 0; index < m_playback.size(); ++index) {
                const int physical = m_playback.at(index);
                const int time = m_times.value(index, m_frametime);
                if (time == m_frametime) {
                    frames.append(physical);
                } else {
                    QJsonObject frame;
                    frame.insert(QStringLiteral("index"), physical);
                    frame.insert(QStringLiteral("time"), time);
                    frames.append(frame);
                }
            }
            animation.insert(QStringLiteral("frames"), frames);
        }

        root.insert(QStringLiteral("animation"), animation);
    } else {
        root.remove(QStringLiteral("animation"));
    }

    return root;
}

TextureDocument::Snapshot TextureDocument::snapshot() const
{
    Snapshot state;
    state.frames = m_frames;
    state.playback = m_playback;
    state.times = m_times;
    state.mcmeta = m_mcmeta;
    state.animated = m_animated;
    state.hasAnimationBlock = m_hasAnimationBlock;
    state.frametime = m_frametime;
    state.interpolate = m_interpolate;
    return state;
}

void TextureDocument::restore(const Snapshot &state)
{
    m_frames = state.frames;
    m_playback = state.playback;
    m_times = state.times;
    m_mcmeta = state.mcmeta;
    m_animated = state.animated;
    m_hasAnimationBlock = state.hasAnimationBlock;
    m_frametime = state.frametime;
    m_interpolate = state.interpolate;
}

bool TextureDocument::statesEqual(const Snapshot &a, const Snapshot &b) const
{
    if (a.animated != b.animated || a.hasAnimationBlock != b.hasAnimationBlock
        || a.frametime != b.frametime || a.interpolate != b.interpolate) {
        return false;
    }
    if (a.playback != b.playback || a.times != b.times)
        return false;
    if (a.mcmeta != b.mcmeta)
        return false;
    if (a.frames.size() != b.frames.size())
        return false;
    for (int index = 0; index < a.frames.size(); ++index) {
        if (a.frames.at(index) != b.frames.at(index))
            return false;
    }
    return true;
}

void TextureDocument::resetHistory()
{
    m_history.clear();
    m_history.append(snapshot());
    m_historyIndex = 0;
    m_savedIndex = 0;
}

void TextureDocument::trimHistory()
{
    while (m_history.size() > kMaxHistoryStates) {
        m_history.removeFirst();
        --m_historyIndex;
        --m_savedIndex;
    }

    const auto historyBytes = [this]() {
        qint64 bytes = 0;
        for (const Snapshot &state : m_history) {
            for (const QImage &image : state.frames)
                bytes += static_cast<qint64>(image.sizeInBytes());
        }
        return bytes;
    };

    while (m_history.size() > 2 && historyBytes() > kMaxHistoryBytes) {
        m_history.removeFirst();
        --m_historyIndex;
        --m_savedIndex;
    }

    if (m_savedIndex < -1)
        m_savedIndex = -1;
}

bool TextureDocument::dirty() const
{
    return m_historyIndex != m_savedIndex;
}

bool TextureDocument::canUndo() const
{
    return m_historyIndex > 0;
}

bool TextureDocument::canRedo() const
{
    return m_historyIndex >= 0 && m_historyIndex + 1 < m_history.size();
}

bool TextureDocument::commitEdit()
{
    if (!m_valid || m_history.isEmpty())
        return false;

    Snapshot current = snapshot();
    if (m_historyIndex >= 0 && m_historyIndex < m_history.size()
        && statesEqual(m_history.at(m_historyIndex), current)) {
        return false;
    }

    while (m_history.size() > m_historyIndex + 1)
        m_history.removeLast();
    m_history.append(std::move(current));
    m_historyIndex = m_history.size() - 1;
    trimHistory();
    return true;
}

bool TextureDocument::undo()
{
    if (!canUndo())
        return false;
    --m_historyIndex;
    restore(m_history.at(m_historyIndex));
    return true;
}

bool TextureDocument::redo()
{
    if (!canRedo())
        return false;
    ++m_historyIndex;
    restore(m_history.at(m_historyIndex));
    return true;
}

} // namespace mcstudio
