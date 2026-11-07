#include "core/ExportService.h"

#include "core/Project.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSaveFile>
#include <QSet>
#include <QtCore/private/qzipwriter_p.h>

namespace mcstudio {

ExportResult ExportService::exportZip(const Project &project, const QString &targetZip)
{
    ExportResult result;

    QMap<QString, QString> files;
    QSet<QString> directories;
    const auto collect = [&files, &directories](const QString &root) {
        if (!QFileInfo::exists(root))
            return;
        QDir base(root);
        QDirIterator iterator(root, QDir::Files | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString absolute = iterator.next();
            const QString rel = QDir::fromNativeSeparators(base.relativeFilePath(absolute));
            files.insert(rel, absolute);
        }
        QDirIterator dirIterator(root, QDir::Dirs | QDir::NoDotAndDotDot,
                                 QDirIterator::Subdirectories);
        while (dirIterator.hasNext()) {
            const QString absolute = dirIterator.next();
            const QString rel = QDir::fromNativeSeparators(base.relativeFilePath(absolute));
            if (!rel.isEmpty())
                directories.insert(rel);
        }
    };

    collect(project.sourceDir);
    collect(project.packDir);

    if (!files.contains(QStringLiteral("pack.mcmeta"))) {
        result.error = QStringLiteral("项目缺少 pack.mcmeta，无法导出");
        return result;
    }

    QBuffer buffer;
    if (!buffer.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("无法创建 ZIP 缓冲区");
        return result;
    }

    QZipWriter writer(&buffer);
    writer.setCompressionPolicy(QZipWriter::AutoCompress);

    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        QFile file(it.value());
        if (!file.open(QIODevice::ReadOnly)) {
            result.error = QStringLiteral("无法读取 %1：%2")
                               .arg(it.value(), file.errorString());
            writer.close();
            return result;
        }
        const QByteArray bytes = file.readAll();
        writer.addFile(it.key(), bytes);
        result.bytes += bytes.size();
        ++result.fileCount;
    }

    // Keep explicitly-created empty folders (for example
    // assets/<namespace>/ selected in the pack editor) in the resulting ZIP.
    // Only dirs with no files are emitted, so a normal imported pack keeps its
    // original file-entry set.
    QStringList sortedDirectories = directories.values();
    sortedDirectories.sort(Qt::CaseInsensitive);
    for (const QString &directory : sortedDirectories) {
        const QString prefix = directory + QLatin1Char('/');
        bool hasFile = false;
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            if (it.key().startsWith(prefix)) {
                hasFile = true;
                break;
            }
        }
        if (!hasFile)
            writer.addDirectory(directory);
    }

    writer.close();
    if (writer.status() != QZipWriter::NoError) {
        result.error = QStringLiteral("ZIP 写入失败（状态 %1）").arg(int(writer.status()));
        return result;
    }

    // QSaveFile gives us atomic replacement, including the 1C "overwrite the
    // original ZIP" path: a failed write leaves the original untouched.
    QSaveFile save(targetZip);
    if (!save.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("无法写入目标 ZIP：%1").arg(save.errorString());
        return result;
    }
    const QByteArray zipData = buffer.data();
    if (save.write(zipData) != zipData.size()) {
        result.error = QStringLiteral("写入目标 ZIP 失败：%1").arg(save.errorString());
        save.cancelWriting();
        return result;
    }
    if (!save.commit()) {
        result.error = QStringLiteral("提交 ZIP 失败：%1").arg(save.errorString());
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace mcstudio
