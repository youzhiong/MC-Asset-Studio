#pragma once

#include "core/Project.h"

#include <QByteArray>
#include <QString>
#include <QVector>
#include <functional>

namespace mcstudio {

struct NewProjectRequest {
    QString name;
    int packFormat = 15;
    QVector<int> supportedFormats;
    QString description;
    QByteArray packPng;
};

struct ProjectCreateResult {
    bool ok = false;
    QString error;
    QString projectDir;
    QString projectId;
};

struct ProjectSummary {
    QString projectDir;
    QString id;
    QString name;
    QString origin; // zip / folder / created
    QString originPath;
    QString updatedAt;
    int packFormat = 15;
};

class ProjectService
{
public:
    using Progress = std::function<void(int done, int total, const QString &message)>;

    static QString newProjectId();
    static QByteArray buildPackMeta(int packFormat, const QVector<int> &supportedFormats,
                                    const QString &description);
    static ProjectCreateResult createBlank(const QString &projectsRoot,
                                           const NewProjectRequest &request);
    static ProjectCreateResult importFolder(const QString &folderPath,
                                            const QString &projectsRoot,
                                            const QString &name,
                                            const Progress &progress = {});
    static QVector<ProjectSummary> listProjects(const QString &projectsRoot);
};

} // namespace mcstudio
