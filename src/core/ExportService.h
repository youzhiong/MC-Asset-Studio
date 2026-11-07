#pragma once

#include <QString>

namespace mcstudio {

class Project;

struct ExportResult {
    bool ok = false;
    QString error;
    int fileCount = 0;
    qint64 bytes = 0;
};

class ExportService
{
public:
    static ExportResult exportZip(const Project &project, const QString &targetZip);
};

} // namespace mcstudio
