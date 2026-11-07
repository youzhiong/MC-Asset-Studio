#include "core/TextureCategory.h"

namespace mcstudio {

QVector<TextureCategoryInfo> textureCategories()
{
    return {
        {QStringLiteral("block"), QStringLiteral("方块")},
        {QStringLiteral("item"), QStringLiteral("物品")},
        {QStringLiteral("entity"), QStringLiteral("实体")},
        {QStringLiteral("gui"), QStringLiteral("界面")},
        {QStringLiteral("environment"), QStringLiteral("环境")},
        {QStringLiteral("effect"), QStringLiteral("效果")},
        {QStringLiteral("font"), QStringLiteral("字体")},
        {QStringLiteral("colormap"), QStringLiteral("色彩映射")},
        {QStringLiteral("map"), QStringLiteral("地图")},
        {QStringLiteral("mob_effect"), QStringLiteral("状态效果")},
        {QStringLiteral("models"), QStringLiteral("模型贴图")},
        {QStringLiteral("painting"), QStringLiteral("画")},
        {QStringLiteral("particle"), QStringLiteral("粒子")},
        {QStringLiteral("trims"), QStringLiteral("盔甲纹饰")},
        {QStringLiteral("misc"), QStringLiteral("杂项")},
        {QStringLiteral("other"), QStringLiteral("其他")},
    };
}

QString textureCategoryForPath(const QString &path)
{
    const QString marker = QStringLiteral("/textures/");
    const int index = path.indexOf(marker);
    if (index < 0)
        return QStringLiteral("other");

    const QString rest = path.mid(index + marker.size());
    const int slash = rest.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return QStringLiteral("other");

    const QString category = rest.left(slash);
    for (const TextureCategoryInfo &info : textureCategories()) {
        if (info.key == category)
            return category;
    }
    return QStringLiteral("other");
}

QString textureCategoryLabel(const QString &key)
{
    for (const TextureCategoryInfo &info : textureCategories()) {
        if (info.key == key)
            return info.label;
    }
    return QStringLiteral("其他");
}

} // namespace mcstudio
