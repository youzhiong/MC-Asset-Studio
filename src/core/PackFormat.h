#pragma once

#include <QString>
#include <QVector>

namespace mcstudio {

// One row of the resource pack format table. A format may cover a version
// range; canonicalVersion is the version used when matching instances.
struct PackFormatInfo {
    int format = 0;
    QString minVersion;
    QString maxVersion;
    QString canonicalVersion;
    QString label;
};

QVector<PackFormatInfo> packFormatTable();
const PackFormatInfo *findPackFormat(int format);
QString mcVersionForPackFormat(int format);
int packFormatForMcVersion(const QString &version);
QString packFormatLabel(int format);
QString packFormatSummary(int format, const QString &prefix = QString());

// Version helpers shared by reference-instance matching.
QString mcVersionFamily(const QString &version); // "1.20.1" -> "1.20"
bool versionIsValid(const QString &version);
int compareMcVersions(const QString &a, const QString &b);

} // namespace mcstudio
