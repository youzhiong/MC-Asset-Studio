#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtGlobal>
#include <memory>

class QZipReader;

namespace mcstudio {

struct ZipEntry {
    QString path;
    qint64 size = 0;
    quint32 crc = 0;
    bool isDir = false;
};

class ZipArchive
{
public:
    explicit ZipArchive(const QString &zipPath);
    ~ZipArchive();

    bool isOpen() const { return m_open; }
    QString error() const { return m_error; }
    QList<ZipEntry> entries() const;
    QByteArray read(const QString &path) const;

private:
    std::unique_ptr<QZipReader> m_reader;
    QString m_error;
    bool m_open = false;
};

} // namespace mcstudio
