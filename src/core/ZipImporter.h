#pragma once

#include <QString>
#include <QVector>
#include <functional>

namespace mcstudio {

struct ImportResult {
    bool ok = false;
    QString error;
    QString projectId;
    QString projectDir;
    QString sourceZip;
    QString name;
    QString description;
    QString cacheManifestPath;
    int packFormat = 15;
    QVector<int> supportedFormats;
    int fileCount = 0;
    bool fromCache = false;
};

class ZipImporter
{
public:
    using Progress = std::function<void(int done, int total, const QString &message)>;

    static ImportResult import(const QString &zipPath, const QString &dataRoot,
                               const Progress &progress = {});
};

} // namespace mcstudio
