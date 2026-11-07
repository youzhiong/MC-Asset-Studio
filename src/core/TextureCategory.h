#pragma once

#include <QString>
#include <QVector>

namespace mcstudio {

struct TextureCategoryInfo {
    QString key;   // directory name: block, item, ...
    QString label; // Chinese display label
};

QVector<TextureCategoryInfo> textureCategories();
QString textureCategoryForPath(const QString &path);
QString textureCategoryLabel(const QString &key);

} // namespace mcstudio
