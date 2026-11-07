#include "core/ModScanner.h"

#include "core/AppPaths.h"
#include "core/AssetNamespace.h"
#include "core/PathUtils.h"
#include "core/ZipArchive.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QStringList>

#include <utility>

namespace mcstudio {
namespace {

QByteArray stripUtf8Bom(QByteArray data)
{
    static const char kUtf8Bom[] = "\xEF\xBB\xBF";
    if (data.startsWith(kUtf8Bom))
        data.remove(0, 3);
    return data;
}

QJsonObject readJsonFile(const QString &path, bool *ok = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return {};
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(stripUtf8Bom(file.readAll()), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        if (ok)
            *ok = false;
        return {};
    }
    if (ok)
        *ok = true;
    return document.object();
}

bool writeJsonFile(const QString &path, const QJsonObject &object)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return file.commit();
}

QString sha1Hex(const QString &text)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString loaderCacheKey(const QString &instanceLoader)
{
    const QString normalized = instanceLoader.trimmed().toLower();
    return normalized.isEmpty() ? QStringLiteral("auto") : normalized;
}

// ---------------------------------------------------------------------------
// Minimal Forge TOML reader (mods.toml / neoforge.mods.toml)
// ---------------------------------------------------------------------------

struct ForgeModEntry {
    QString modId;
    QString displayName;
    QString version;
    QString logoFile;
    QString description;
    QString authors;
    QString credits;
};

struct ForgeTomlData {
    QString modLoader;
    QString loaderVersion;
    QVector<ForgeModEntry> mods;
    QHash<QString, QString> minecraftRanges; // modId -> versionRange of minecraft
};

QString unescapeTomlBasic(const QString &body)
{
    QString out;
    out.reserve(body.size());
    for (int i = 0; i < body.size(); ++i) {
        const QChar c = body.at(i);
        if (c != QLatin1Char('\\')) {
            out += c;
            continue;
        }
        if (i + 1 >= body.size())
            break;
        const QChar next = body.at(++i);
        switch (next.unicode()) {
        case 'b':
            out += QLatin1Char('\b');
            break;
        case 't':
            out += QLatin1Char('\t');
            break;
        case 'n':
            out += QLatin1Char('\n');
            break;
        case 'f':
            out += QLatin1Char('\f');
            break;
        case 'r':
            out += QLatin1Char('\r');
            break;
        case '"':
            out += QLatin1Char('"');
            break;
        case '\\':
            out += QLatin1Char('\\');
            break;
        case 'u':
        case 'U': {
            const int digits = next == QLatin1Char('u') ? 4 : 8;
            if (i + digits < body.size()) {
                bool ok = false;
                const uint code = body.mid(i + 1, digits).toUInt(&ok, 16);
                if (ok) {
                    const char32_t codepoint = static_cast<char32_t>(code);
                    out += QString::fromUcs4(&codepoint, 1);
                    i += digits;
                }
            }
            break;
        }
        default:
            out += next;
            break;
        }
    }
    return out;
}

QString tomlScalar(const QString &raw)
{
    const QString text = raw.trimmed();
    if (text.isEmpty())
        return {};

    if (text.startsWith(QLatin1String("\"\"\""))) {
        const int end = text.indexOf(QLatin1String("\"\"\""), 3);
        const QString body = end >= 0 ? text.mid(3, end - 3) : text.mid(3);
        return unescapeTomlBasic(body);
    }
    if (text.startsWith(QLatin1String("'''"))) {
        const int end = text.indexOf(QLatin1String("'''"), 3);
        return end >= 0 ? text.mid(3, end - 3) : text.mid(3);
    }
    if (text.startsWith(QLatin1Char('"'))) {
        QString out;
        bool escaped = false;
        for (int i = 1; i < text.size(); ++i) {
            const QChar c = text.at(i);
            if (escaped) {
                escaped = false;
                out += c;
                continue;
            }
            if (c == QLatin1Char('\\')) {
                escaped = true;
                continue;
            }
            if (c == QLatin1Char('"'))
                return out;
            out += c;
        }
        return out;
    }
    if (text.startsWith(QLatin1Char('\''))) {
        const int end = text.indexOf(QLatin1Char('\''), 1);
        return end > 0 ? text.mid(1, end - 1) : text.mid(1);
    }

    int end = 0;
    while (end < text.size() && !text.at(end).isSpace() && text.at(end) != QLatin1Char('#'))
        ++end;
    return text.left(end);
}

int findTomlAssignment(const QString &line)
{
    bool inBasic = false;
    bool inLiteral = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (inBasic) {
            if (c == QLatin1Char('\\')) {
                ++i;
                continue;
            }
            if (c == QLatin1Char('"'))
                inBasic = false;
            continue;
        }
        if (inLiteral) {
            if (c == QLatin1Char('\''))
                inLiteral = false;
            continue;
        }
        if (c == QLatin1Char('"')) {
            inBasic = true;
            continue;
        }
        if (c == QLatin1Char('\'')) {
            inLiteral = true;
            continue;
        }
        if (c == QLatin1Char('='))
            return i;
    }
    return -1;
}

QString stripQuotes(QString text)
{
    text = text.trimmed();
    if (text.size() >= 2
        && ((text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"')))
            || (text.startsWith(QLatin1Char('\'')) && text.endsWith(QLatin1Char('\''))))) {
        return text.mid(1, text.size() - 2);
    }
    return text;
}

ForgeTomlData parseForgeToml(const QByteArray &bytes)
{
    ForgeTomlData data;
    QString text = QString::fromUtf8(bytes);
    if (!text.isEmpty() && text.at(0) == QChar(0xFEFF))
        text.remove(0, 1);

    const QStringList lines = text.split(QLatin1Char('\n'));
    enum class Section { None, Mods, Dependencies };
    Section section = Section::None;
    int currentMod = -1;
    QString dependencyOwner;
    QString dependencyModId;
    QString pendingRange;

    for (int i = 0; i < lines.size(); ++i) {
        QString line = lines.at(i);
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;

        if (trimmed.startsWith(QLatin1Char('['))) {
            if (trimmed.startsWith(QLatin1String("[["))) {
                const int close = trimmed.indexOf(QLatin1String("]]"));
                if (close < 0)
                    continue;
                const QString header = trimmed.mid(2, close - 2).trimmed();
                if (header.compare(QLatin1String("mods"), Qt::CaseInsensitive) == 0) {
                    data.mods.append(ForgeModEntry{});
                    currentMod = static_cast<int>(data.mods.size()) - 1;
                    section = Section::Mods;
                } else if (header.startsWith(QLatin1String("dependencies."),
                                             Qt::CaseInsensitive)) {
                    dependencyOwner = stripQuotes(header.mid(13));
                    dependencyModId.clear();
                    pendingRange.clear();
                    section = Section::Dependencies;
                } else if (header.compare(QLatin1String("dependencies"),
                                          Qt::CaseInsensitive) == 0) {
                    dependencyOwner.clear();
                    dependencyModId.clear();
                    pendingRange.clear();
                    section = Section::Dependencies;
                } else {
                    section = Section::None;
                    currentMod = -1;
                }
            } else {
                // Any normal table ends the current array-of-tables section.
                section = Section::None;
                currentMod = -1;
            }
            continue;
        }

        const int assignment = findTomlAssignment(trimmed);
        if (assignment < 0)
            continue;
        const QString key = trimmed.left(assignment).trimmed();
        QString rawValue = trimmed.mid(assignment + 1).trimmed();

        if (rawValue.startsWith(QLatin1String("\"\"\""))
            || rawValue.startsWith(QLatin1String("'''"))) {
            const QString delim = rawValue.left(3);
            QString buffer = rawValue;
            while (buffer.indexOf(delim, 3) < 0 && i + 1 < lines.size()) {
                ++i;
                QString next = lines.at(i);
                if (next.endsWith(QLatin1Char('\r')))
                    next.chop(1);
                buffer += QLatin1Char('\n') + next;
            }
            rawValue = buffer;
        }
        const QString value = tomlScalar(rawValue);

        if (section == Section::Mods && currentMod >= 0) {
            ForgeModEntry &mod = data.mods[currentMod];
            if (key == QLatin1String("modId"))
                mod.modId = value;
            else if (key == QLatin1String("displayName"))
                mod.displayName = value;
            else if (key == QLatin1String("version"))
                mod.version = value;
            else if (key == QLatin1String("logoFile"))
                mod.logoFile = value;
            else if (key == QLatin1String("description"))
                mod.description = value;
            else if (key == QLatin1String("authors"))
                mod.authors = value;
            else if (key == QLatin1String("credits") && mod.authors.isEmpty())
                mod.authors = value;
        } else if (section == Section::Dependencies) {
            if (key == QLatin1String("modId")) {
                dependencyModId = value;
                if (dependencyModId == QLatin1String("minecraft")
                    && !dependencyOwner.isEmpty() && !pendingRange.isEmpty()) {
                    data.minecraftRanges.insert(dependencyOwner, pendingRange);
                }
            } else if (key == QLatin1String("versionRange")) {
                pendingRange = value;
                if (dependencyModId == QLatin1String("minecraft")
                    && !dependencyOwner.isEmpty()) {
                    data.minecraftRanges.insert(dependencyOwner, value);
                }
            }
        } else {
            if (key == QLatin1String("modLoader"))
                data.modLoader = value;
            else if (key == QLatin1String("loaderVersion"))
                data.loaderVersion = value;
        }
    }
    return data;
}

// ---------------------------------------------------------------------------
// Other metadata formats
// ---------------------------------------------------------------------------

QString authorsValue(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    QStringList authors;
    for (const QJsonValue &entry : value.toArray()) {
        if (entry.isString())
            authors.append(entry.toString());
        else if (entry.isObject()) {
            const QString name = entry.toObject().value(QStringLiteral("name")).toString();
            if (!name.isEmpty())
                authors.append(name);
        }
    }
    return authors.join(QStringLiteral(", "));
}

QString rangeValue(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isArray()) {
        QStringList parts;
        for (const QJsonValue &entry : value.toArray()) {
            if (entry.isString())
                parts.append(entry.toString());
        }
        return parts.join(QStringLiteral(", "));
    }
    return {};
}

QVector<ModInfo> parseForgeMetadata(const QByteArray &bytes, const QString &source)
{
    const ForgeTomlData data = parseForgeToml(bytes);
    QVector<ModInfo> mods;
    for (const ForgeModEntry &entry : data.mods) {
        if (entry.modId.trimmed().isEmpty())
            continue;
        ModInfo mod;
        mod.modId = entry.modId.trimmed();
        mod.displayName = entry.displayName.trimmed();
        if (mod.displayName.isEmpty())
            mod.displayName = mod.modId;
        mod.version = entry.version;
        mod.description = entry.description;
        mod.authors = entry.authors;
        mod.logoFile = entry.logoFile;
        mod.loader = data.modLoader.isEmpty() ? QStringLiteral("javafml")
                                              : data.modLoader.toLower();
        mod.minecraftRange = data.minecraftRanges.value(mod.modId);
        mod.metadataSource = source;
        mods.append(mod);
    }
    return mods;
}

QVector<ModInfo> parseFabricMetadata(const QByteArray &bytes, const QString &source)
{
    const QJsonDocument document = QJsonDocument::fromJson(stripUtf8Bom(bytes));
    if (!document.isObject())
        return {};
    const QJsonObject object = document.object();

    ModInfo mod;
    mod.modId = object.value(QStringLiteral("id")).toString().trimmed();
    if (mod.modId.isEmpty())
        return {};
    mod.displayName = object.value(QStringLiteral("name")).toString().trimmed();
    if (mod.displayName.isEmpty())
        mod.displayName = mod.modId;
    mod.version = object.value(QStringLiteral("version")).toString();
    mod.description = object.value(QStringLiteral("description")).toString();
    mod.authors = authorsValue(object.value(QStringLiteral("authors")));
    mod.logoFile = object.value(QStringLiteral("icon")).toString();
    mod.loader = QStringLiteral("fabric");
    mod.metadataSource = source;
    mod.minecraftRange = rangeValue(
        object.value(QStringLiteral("depends")).toObject().value(QStringLiteral("minecraft")));
    return {mod};
}

QVector<ModInfo> parseQuiltMetadata(const QByteArray &bytes, const QString &source)
{
    const QJsonDocument document = QJsonDocument::fromJson(stripUtf8Bom(bytes));
    if (!document.isObject())
        return {};
    const QJsonObject root = document.object();
    const QJsonObject loader = root.value(QStringLiteral("quilt_loader")).toObject();
    if (loader.isEmpty())
        return parseFabricMetadata(bytes, source);

    ModInfo mod;
    mod.modId = loader.value(QStringLiteral("id")).toString().trimmed();
    if (mod.modId.isEmpty())
        return {};
    mod.version = loader.value(QStringLiteral("version")).toString();
    const QJsonObject metadata = loader.value(QStringLiteral("metadata")).toObject();
    mod.displayName = metadata.value(QStringLiteral("name")).toString().trimmed();
    if (mod.displayName.isEmpty())
        mod.displayName = mod.modId;
    mod.description = metadata.value(QStringLiteral("description")).toString();
    mod.authors = authorsValue(metadata.value(QStringLiteral("contributors")));
    mod.logoFile = metadata.value(QStringLiteral("icon")).toString();
    mod.loader = QStringLiteral("quilt");
    mod.metadataSource = source;

    for (const QJsonValue &entry : loader.value(QStringLiteral("depends")).toArray()) {
        const QJsonObject dependency = entry.toObject();
        if (dependency.value(QStringLiteral("id")).toString() == QLatin1String("minecraft"))
            mod.minecraftRange = rangeValue(dependency.value(QStringLiteral("versions")));
    }
    return {mod};
}

QVector<ModInfo> parseMcmodInfo(const QByteArray &bytes)
{
    const QJsonDocument document = QJsonDocument::fromJson(stripUtf8Bom(bytes));
    QJsonArray entries;
    if (document.isArray()) {
        entries = document.array();
    } else if (document.isObject()) {
        entries = document.object().value(QStringLiteral("modList")).toArray();
    }
    if (entries.isEmpty())
        return {};

    QVector<ModInfo> mods;
    for (const QJsonValue &value : entries) {
        const QJsonObject object = value.toObject();
        const QString modId = object.value(QStringLiteral("modid")).toString().trimmed();
        if (modId.isEmpty())
            continue;
        ModInfo mod;
        mod.modId = modId;
        mod.displayName = object.value(QStringLiteral("name")).toString().trimmed();
        if (mod.displayName.isEmpty())
            mod.displayName = modId;
        mod.version = object.value(QStringLiteral("version")).toString();
        mod.description = object.value(QStringLiteral("description")).toString();
        mod.authors = authorsValue(object.value(QStringLiteral("authorList")));
        mod.logoFile = object.value(QStringLiteral("logoFile")).toString();
        mod.loader = QStringLiteral("legacy");
        mod.metadataSource = QStringLiteral("mcmod.info");

        const QString mcVersion = object.value(QStringLiteral("mcversion")).toString();
        if (!mcVersion.isEmpty())
            mod.minecraftRange = QStringLiteral("[%1]").arg(mcVersion);
        for (const QJsonValue &dependency :
             object.value(QStringLiteral("dependencies")).toArray()) {
            const QString text = dependency.toString();
            if (text.startsWith(QLatin1String("minecraft@")))
                mod.minecraftRange = text.mid(10);
        }
        mods.append(mod);
    }
    return mods;
}

QVector<ModInfo> parseManifestMetadata(const QByteArray &bytes)
{
    const QString text = QString::fromUtf8(bytes);
    QStringList unfolded;
    for (QString line : text.split(QLatin1Char('\n'))) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (!line.isEmpty() && line.startsWith(QLatin1Char(' ')) && !unfolded.isEmpty())
            unfolded.last() += line.mid(1);
        else
            unfolded.append(line);
    }

    QHash<QString, QString> values;
    for (const QString &line : std::as_const(unfolded)) {
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon <= 0)
            continue;
        values.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }

    const QString title = values.value(QStringLiteral("implementation-title"));
    const QString version = values.value(QStringLiteral("implementation-version"));
    if (title.isEmpty() && version.isEmpty())
        return {};

    ModInfo mod;
    mod.displayName = title.isEmpty() ? QStringLiteral("未知模组") : title;
    mod.version = version;
    mod.loader = QStringLiteral("manifest");
    mod.metadataSource = QStringLiteral("MANIFEST.MF");
    mod.status = QStringLiteral("unknown");
    return {mod};
}

QStringList metadataCandidates(const QString &instanceLoader)
{
    QStringList candidates;
    if (instanceLoader == QLatin1String("fabric")
        || instanceLoader == QLatin1String("quilt")) {
        candidates << QStringLiteral("fabric.mod.json") << QStringLiteral("quilt.mod.json");
    }
    candidates << QStringLiteral("META-INF/neoforge.mods.toml")
               << QStringLiteral("META-INF/mods.toml") << QStringLiteral("fabric.mod.json")
               << QStringLiteral("quilt.mod.json") << QStringLiteral("mcmod.info")
               << QStringLiteral("META-INF/MANIFEST.MF");
    candidates.removeDuplicates();
    return candidates;
}

QHash<QString, int> namespaceCountsFromEntries(const QList<ZipEntry> &entries,
                                              QStringList *namespaces)
{
    QHash<QString, int> counts;
    for (const ZipEntry &entry : entries) {
        if (entry.isDir)
            continue;
        const QString ns = namespaceFromAssetPath(entry.path);
        if (ns.isEmpty())
            continue;
        counts[ns] = counts.value(ns) + 1;
    }
    if (namespaces) {
        namespaces->clear();
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
            namespaces->append(it.key());
        namespaces->sort(Qt::CaseInsensitive);
    }
    return counts;
}

QHash<QString, QString> buildEntryLookup(const QList<ZipEntry> &entries)
{
    QHash<QString, QString> lookup;
    for (const ZipEntry &entry : entries) {
        if (entry.isDir)
            continue;
        const QString path = QDir::fromNativeSeparators(entry.path);
        lookup.insert(path.toLower(), entry.path);
    }
    return lookup;
}

void finalizeMod(ModInfo &mod, const JarModScan &scan,
                 const QHash<QString, QString> &entryLookup)
{
    mod.jarPath = scan.jarPath;
    mod.fileName = scan.fileName;
    mod.instanceKey = scan.instanceKey;
    mod.origin = scan.origin;
    mod.namespaces = scan.namespaces;

    QString logo = QDir::fromNativeSeparators(mod.logoFile);
    while (logo.startsWith(QLatin1Char('/')))
        logo.remove(0, 1);
    mod.logoFile = entryLookup.value(logo.toLower());

    if (mod.logoFile.isEmpty() && !mod.modId.isEmpty()) {
        const QStringList candidates = {
            QStringLiteral("icon.png"),
            QStringLiteral("logo.png"),
            QStringLiteral("assets/%1/icon.png").arg(mod.modId),
            QStringLiteral("assets/%1/logo.png").arg(mod.modId),
            QStringLiteral("assets/%1/textures/icon.png").arg(mod.modId),
        };
        for (const QString &candidate : candidates) {
            const QString found = entryLookup.value(candidate.toLower());
            if (!found.isEmpty()) {
                mod.logoFile = found;
                break;
            }
        }
    }
}

} // namespace

QString ModScanner::cacheDirectory()
{
    return AppPaths::gameModsRoot();
}

QString ModScanner::cacheFile(const QString &jarPath, const QString &instanceLoader)
{
    const QString key = normalizedPath(jarPath).toLower() + QLatin1Char('|')
                        + loaderCacheKey(instanceLoader);
    return QDir(cacheDirectory()).filePath(sha1Hex(key) + QStringLiteral(".json"));
}

JarModScan ModScanner::scanJar(const ModFileInfo &file, const QString &instanceKey,
                               const QString &instanceLoader, bool forceRefresh,
                               bool useCache)
{
    JarModScan scan;
    scan.jarPath = normalizedPath(file.path);
    scan.fileName = file.fileName.isEmpty() ? QFileInfo(scan.jarPath).fileName() : file.fileName;
    scan.instanceKey = instanceKey;
    scan.origin = file.origin;

    const QFileInfo jarInfo(scan.jarPath);
    if (!jarInfo.isFile()) {
        scan.status = QStringLiteral("error");
        scan.error = QStringLiteral("模组 jar 不存在：%1").arg(scan.jarPath);
        return scan;
    }
    scan.size = jarInfo.size();
    scan.mtime = jarInfo.lastModified().toMSecsSinceEpoch();

    if (useCache && !forceRefresh) {
        bool ok = false;
        const QJsonObject cachedObject =
            readJsonFile(cacheFile(scan.jarPath, instanceLoader), &ok);
        if (ok && cachedObject.value(QStringLiteral("schema")).toInt() == 2) {
            JarModScan cached = JarModScan::fromJson(cachedObject);
            if (cached.size == scan.size && cached.mtime == scan.mtime
                && cached.status != QLatin1String("error")) {
                cached.fromCache = true;
                cached.jarPath = scan.jarPath;
                cached.fileName = scan.fileName;
                cached.instanceKey = scan.instanceKey;
                cached.origin = scan.origin;
                return cached;
            }
        }
    }

    ZipArchive archive(scan.jarPath);
    if (!archive.isOpen()) {
        scan.status = QStringLiteral("error");
        scan.error = archive.error();
        return scan;
    }

    const QList<ZipEntry> entries = archive.entries();
    scan.namespaceCounts = namespaceCountsFromEntries(entries, &scan.namespaces);
    const QHash<QString, QString> entryLookup = buildEntryLookup(entries);

    QVector<ModInfo> mods;
    for (const QString &candidate : metadataCandidates(instanceLoader)) {
        const QString entryPath = entryLookup.value(candidate.toLower());
        if (entryPath.isEmpty())
            continue;
        const QByteArray bytes = archive.read(entryPath);
        if (bytes.isEmpty())
            continue;

        QVector<ModInfo> parsed;
        if (candidate.endsWith(QLatin1String("mods.toml")))
            parsed = parseForgeMetadata(bytes, candidate);
        else if (candidate == QLatin1String("fabric.mod.json"))
            parsed = parseFabricMetadata(bytes, candidate);
        else if (candidate == QLatin1String("quilt.mod.json"))
            parsed = parseQuiltMetadata(bytes, candidate);
        else if (candidate == QLatin1String("mcmod.info"))
            parsed = parseMcmodInfo(bytes);
        else if (candidate == QLatin1String("META-INF/MANIFEST.MF"))
            parsed = parseManifestMetadata(bytes);

        if (!parsed.isEmpty()) {
            mods = parsed;
            break;
        }
    }

    for (ModInfo &mod : mods)
        finalizeMod(mod, scan, entryLookup);

    scan.mods = mods;
    if (mods.isEmpty()) {
        scan.status = QStringLiteral("unknown");
        scan.error = scan.namespaces.isEmpty()
                         ? QStringLiteral("未找到 mod 元数据与 assets 命名空间")
                         : QStringLiteral("未找到 mod 元数据");
    } else {
        bool hasModId = false;
        for (const ModInfo &mod : mods) {
            if (!mod.modId.isEmpty()) {
                hasModId = true;
                break;
            }
        }
        scan.status = hasModId ? QStringLiteral("ok") : QStringLiteral("unknown");
    }

    scan.scannedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    scan.fromCache = false;

    if (scan.status != QLatin1String("error"))
        writeJsonFile(cacheFile(scan.jarPath, instanceLoader), scan.toJson());
    return scan;
}

void ModScanner::clearCache()
{
    QDir(cacheDirectory()).removeRecursively();
    QDir().mkpath(cacheDirectory());
}

int ModScanner::cacheFileCount()
{
    int count = 0;
    QDirIterator iterator(cacheDirectory(), QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        ++count;
    }
    return count;
}

} // namespace mcstudio
