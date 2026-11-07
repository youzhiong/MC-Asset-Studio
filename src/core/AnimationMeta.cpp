#include "core/AnimationMeta.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QtMath>

namespace mcstudio {

AnimationMeta AnimationMeta::fromMcmeta(const QByteArray &mcmeta, const QSize &imageSize)
{
    AnimationMeta result;
    if (mcmeta.isEmpty() || imageSize.width() <= 0 || imageSize.height() <= 0)
        return result;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(mcmeta, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return result;

    const QJsonValue animationValue =
        document.object().value(QStringLiteral("animation"));
    if (!animationValue.isObject())
        return result;
    const QJsonObject animation = animationValue.toObject();

    result.frametime = qBound(1, animation.value(QStringLiteral("frametime")).toInt(1), 1000);
    result.interpolate = animation.value(QStringLiteral("interpolate")).toBool(false);

    const int imageWidth = imageSize.width();
    const int imageHeight = imageSize.height();
    result.frameHeight = imageWidth > 0 ? imageWidth : imageHeight;
    result.physicalFrames = (imageWidth > 0 && imageHeight % imageWidth == 0)
                                ? imageHeight / imageWidth
                                : 0;

    const QJsonArray frameValues = animation.value(QStringLiteral("frames")).toArray();

    if (!frameValues.isEmpty()) {
        result.frames.reserve(frameValues.size());
        for (const QJsonValue &value : frameValues) {
            AnimationFrame frame;
            frame.time = result.frametime;
            if (value.isDouble()) {
                frame.index = value.toInt();
            } else if (value.isObject()) {
                const QJsonObject object = value.toObject();
                frame.index = object.value(QStringLiteral("index")).toInt();
                frame.time = qBound(1, object.value(QStringLiteral("time")).toInt(result.frametime),
                                    1000);
            } else {
                continue;
            }
            if (frame.index < 0)
                frame.index = 0;
            if (result.physicalFrames > 0)
                frame.index = frame.index % result.physicalFrames;
            result.frames.append(frame);
        }
    } else if (result.physicalFrames > 1) {
        result.frames.reserve(result.physicalFrames);
        for (int index = 0; index < result.physicalFrames; ++index) {
            AnimationFrame frame;
            frame.index = index;
            frame.time = result.frametime;
            result.frames.append(frame);
        }
    }

    // A one-frame "animation" is effectively static; don't animate it.
    result.valid = result.frames.size() > 1 && result.physicalFrames > 1;
    return result;
}

} // namespace mcstudio
