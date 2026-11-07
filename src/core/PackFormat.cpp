#include "core/PackFormat.h"

#include <QStringList>

namespace mcstudio {
namespace {

// Resource pack format history. This table is intentionally conservative:
// only rows that have been cross-checked are listed, and unknown formats are
// reported as unknown instead of being guessed. Manual instance selection is
// always available for formats or snapshots that are not in this table.
const QVector<PackFormatInfo> &table()
{
    static const QVector<PackFormatInfo> data = {
        {1, QStringLiteral("1.6.1"), QStringLiteral("1.8.9"), QStringLiteral("1.8.9"),
         QStringLiteral("1.6.1 – 1.8.9")},
        {2, QStringLiteral("1.9"), QStringLiteral("1.10.2"), QStringLiteral("1.10.2"),
         QStringLiteral("1.9 – 1.10.2")},
        {3, QStringLiteral("1.11"), QStringLiteral("1.12.2"), QStringLiteral("1.12.2"),
         QStringLiteral("1.11 – 1.12.2")},
        {4, QStringLiteral("1.13"), QStringLiteral("1.14.4"), QStringLiteral("1.14.4"),
         QStringLiteral("1.13 – 1.14.4")},
        {5, QStringLiteral("1.15"), QStringLiteral("1.16.1"), QStringLiteral("1.16.1"),
         QStringLiteral("1.15 – 1.16.1")},
        {6, QStringLiteral("1.16.2"), QStringLiteral("1.16.5"), QStringLiteral("1.16.5"),
         QStringLiteral("1.16.2 – 1.16.5")},
        {7, QStringLiteral("1.17"), QStringLiteral("1.17.1"), QStringLiteral("1.17.1"),
         QStringLiteral("1.17 – 1.17.1")},
        {8, QStringLiteral("1.18"), QStringLiteral("1.18.2"), QStringLiteral("1.18.2"),
         QStringLiteral("1.18 – 1.18.2")},
        {9, QStringLiteral("1.19"), QStringLiteral("1.19.2"), QStringLiteral("1.19.2"),
         QStringLiteral("1.19 – 1.19.2")},
        {12, QStringLiteral("1.19.3"), QStringLiteral("1.19.3"), QStringLiteral("1.19.3"),
         QStringLiteral("1.19.3")},
        {13, QStringLiteral("1.19.4"), QStringLiteral("1.19.4"), QStringLiteral("1.19.4"),
         QStringLiteral("1.19.4")},
        {15, QStringLiteral("1.20"), QStringLiteral("1.20.1"), QStringLiteral("1.20.1"),
         QStringLiteral("1.20 – 1.20.1")},
        {18, QStringLiteral("1.20.2"), QStringLiteral("1.20.2"), QStringLiteral("1.20.2"),
         QStringLiteral("1.20.2")},
        {22, QStringLiteral("1.20.3"), QStringLiteral("1.20.4"), QStringLiteral("1.20.4"),
         QStringLiteral("1.20.3 – 1.20.4")},
        {32, QStringLiteral("1.20.5"), QStringLiteral("1.20.6"), QStringLiteral("1.20.6"),
         QStringLiteral("1.20.5 – 1.20.6")},
        {34, QStringLiteral("1.21"), QStringLiteral("1.21.1"), QStringLiteral("1.21.1"),
         QStringLiteral("1.21 – 1.21.1")},
        {42, QStringLiteral("1.21.2"), QStringLiteral("1.21.3"), QStringLiteral("1.21.3"),
         QStringLiteral("1.21.2 – 1.21.3")},
        {46, QStringLiteral("1.21.4"), QStringLiteral("1.21.4"), QStringLiteral("1.21.4"),
         QStringLiteral("1.21.4")},
    };
    return data;
}

QStringList versionParts(const QString &version)
{
    QString cleaned = version.trimmed();
    // Cut pre-release suffixes such as "1.20.1-pre1" or "1.20.1-rc1".
    const int dash = cleaned.indexOf(QLatin1Char('-'));
    if (dash > 0)
        cleaned = cleaned.left(dash);
    return cleaned.split(QLatin1Char('.'), Qt::SkipEmptyParts);
}

} // namespace

QVector<PackFormatInfo> packFormatTable()
{
    return table();
}

const PackFormatInfo *findPackFormat(int format)
{
    for (const PackFormatInfo &info : table()) {
        if (info.format == format)
            return &info;
    }
    return nullptr;
}

QString mcVersionForPackFormat(int format)
{
    if (const PackFormatInfo *info = findPackFormat(format))
        return info->canonicalVersion;
    return {};
}

int packFormatForMcVersion(const QString &version)
{
    if (!versionIsValid(version))
        return 0;
    for (const PackFormatInfo &info : table()) {
        if (compareMcVersions(version, info.minVersion) >= 0
            && compareMcVersions(version, info.maxVersion) <= 0) {
            return info.format;
        }
    }
    return 0;
}

QString packFormatLabel(int format)
{
    if (const PackFormatInfo *info = findPackFormat(format))
        return info->label;
    return QStringLiteral("未知");
}

QString packFormatSummary(int format, const QString &prefix)
{
    if (format <= 0)
        return QStringLiteral("%1未知格式").arg(prefix);
    const QString label = packFormatLabel(format);
    if (const PackFormatInfo *info = findPackFormat(format)) {
        return QStringLiteral("%1pack_format %2 · Java %3")
            .arg(prefix)
            .arg(format)
            .arg(label.isEmpty() ? info->canonicalVersion : label);
    }
    return QStringLiteral("%1pack_format %2 · 未知版本").arg(prefix).arg(format);
}

bool versionIsValid(const QString &version)
{
    const QStringList parts = versionParts(version);
    if (parts.isEmpty() || parts.size() > 3)
        return false;
    bool ok = false;
    parts.first().toInt(&ok);
    return ok;
}

int compareMcVersions(const QString &a, const QString &b)
{
    const QStringList left = versionParts(a);
    const QStringList right = versionParts(b);
    const int count = qMax(left.size(), right.size());
    for (int index = 0; index < count; ++index) {
        const int leftPart = index < left.size() ? left.at(index).toInt() : 0;
        const int rightPart = index < right.size() ? right.at(index).toInt() : 0;
        if (leftPart != rightPart)
            return leftPart < rightPart ? -1 : 1;
    }
    return 0;
}

QString mcVersionFamily(const QString &version)
{
    const QStringList parts = versionParts(version);
    if (parts.isEmpty())
        return {};
    if (parts.size() == 1)
        return parts.first();
    return QStringLiteral("%1.%2").arg(parts.at(0), parts.at(1));
}

} // namespace mcstudio
