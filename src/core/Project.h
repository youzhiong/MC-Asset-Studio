#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mcstudio {

struct ProjectFile {
    QString path;
    QString source; // "source" or "pack"
    qint64 size = 0;
    qint64 mtime = 0;
    bool isTexture = false;
    bool isJson = false;
};

class Project
{
public:
    bool load(const QString &projectDir, QString *error = nullptr);
    bool save(QString *error = nullptr) const;
    bool loadReferenceConfig(QString *error = nullptr);
    bool saveReferenceConfig(QString *error = nullptr) const;
    void scan();

    bool exists(const QString &relativePath) const;
    QByteArray readBytes(const QString &relativePath) const;
    QString absolutePath(const QString &relativePath) const;
    QString absoluteSourcePath(const QString &relativePath) const;
    QString absoluteOutputPath(const QString &relativePath) const;

    // Writes/removes a file in the pack/ overlay. The path is validated so
    // editors cannot escape the project directory; source/ is never touched.
    bool writeOutputFile(const QString &relativePath, const QByteArray &bytes,
                         QString *error = nullptr) const;
    bool removeOutputFile(const QString &relativePath) const;
    QStringList files() const { return m_paths; }
    const QHash<QString, ProjectFile> &fileIndex() const { return m_files; }

    QString id;
    QString name;
    QString rootDir;
    QString sourceDir;
    QString packDir;
    QString sourceZip;
    QString zipSha1;
    QString packRoot;
    QString origin = QStringLiteral("zip"); // zip / folder / created
    QString originPath;
    QStringList supportedNamespaces; // pack editor: namespaces marked as supported
    QString description;
    QString contentFingerprint;
    QString cacheManifestPath;
    QString createdAt;
    QString updatedAt;
    int packFormat = 15;
    QVector<int> supportedFormats;
    QString referenceInstanceKey; // project-level override, empty = automatic
    int fileCount = 0;
    bool valid = false;
    QString lastError;

private:
    QHash<QString, ProjectFile> m_files;
    QStringList m_paths;
};

} // namespace mcstudio
