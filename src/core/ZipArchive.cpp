#include "core/ZipArchive.h"

#include <QtCore/private/qzipreader_p.h>

namespace mcstudio {

ZipArchive::ZipArchive(const QString &zipPath)
{
    m_reader = std::make_unique<QZipReader>(zipPath);
    if (!m_reader->isReadable()) {
        m_error = QStringLiteral("无法读取 ZIP：%1").arg(zipPath);
        m_reader.reset();
        return;
    }
    m_open = true;
}

ZipArchive::~ZipArchive() = default;

QList<ZipEntry> ZipArchive::entries() const
{
    QList<ZipEntry> result;
    if (!m_reader)
        return result;

    const auto infos = m_reader->fileInfoList();
    result.reserve(infos.size());
    for (const auto &info : infos) {
        ZipEntry entry;
        entry.path = info.filePath;
        entry.size = info.size;
        entry.crc = info.crc;
        entry.isDir = info.isDir;
        result.append(entry);
    }
    return result;
}

QByteArray ZipArchive::read(const QString &path) const
{
    if (!m_reader)
        return {};
    return m_reader->fileData(path);
}

} // namespace mcstudio
