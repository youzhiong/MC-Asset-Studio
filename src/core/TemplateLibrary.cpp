#include "core/TemplateLibrary.h"

#include "core/AppPaths.h"
#include "core/ZipArchive.h"

#include <QBuffer>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLinearGradient>
#include <QPainter>
#include <QPolygon>
#include <QRegularExpression>
#include <QtCore/private/qzipwriter_p.h>

namespace mcstudio {
namespace {

QImage makeCanvas()
{
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    return image;
}

QColor gray(double level, int alpha = 255)
{
    const int value = qBound(0, qRound(level * 255.0), 255);
    return QColor(value, value, value, alpha);
}

QString uniqueTarget(const QString &directory, const QString &fileName)
{
    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QStringLiteral("zip") : info.suffix();
    QString candidate = QDir(directory).filePath(base + QLatin1Char('.') + suffix);
    int counter = 2;
    while (QFileInfo::exists(candidate)) {
        candidate = QDir(directory).filePath(
            QStringLiteral("%1-%2.%3").arg(base).arg(counter++).arg(suffix));
    }
    return candidate;
}

} // namespace

QString TemplateLibrary::root()
{
    return QDir(AppPaths::dataRoot()).filePath(QStringLiteral("templates"));
}

bool TemplateLibrary::ensureRoot(QString *error)
{
    if (!QDir().mkpath(root())) {
        if (error)
            *error = QStringLiteral("无法创建模板目录：%1").arg(root());
        return false;
    }
    return true;
}

QImage TemplateLibrary::makeIngot()
{
    QImage image = makeCanvas();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QPolygon body({QPoint(2, 6), QPoint(12, 3), QPoint(14, 9), QPoint(4, 12)});
    QLinearGradient gradient(0, 3, 0, 13);
    gradient.setColorAt(0.0, gray(0.95));
    gradient.setColorAt(0.35, gray(0.78));
    gradient.setColorAt(1.0, gray(0.42));
    painter.setPen(QPen(gray(0.22), 1));
    painter.setBrush(gradient);
    painter.drawPolygon(body);

    painter.setPen(QPen(gray(1.0), 1));
    painter.drawLine(3, 6, 12, 4);
    painter.setPen(QPen(gray(0.32), 1));
    painter.drawLine(4, 11, 13, 9);
    painter.setPen(QPen(gray(0.62), 1));
    painter.drawLine(6, 6, 5, 10);
    painter.end();
    return image;
}

QImage TemplateLibrary::makeOre()
{
    QImage image = makeCanvas();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.setPen(QPen(gray(0.24), 1));
    painter.setBrush(gray(0.46));
    painter.drawRoundedRect(QRect(1, 1, 14, 14), 2, 2);

    painter.setPen(Qt::NoPen);
    painter.setBrush(gray(0.62, 140));
    painter.drawRect(2, 2, 5, 2);
    painter.setBrush(gray(0.30, 150));
    painter.drawRect(9, 10, 5, 3);

    const QVector<QPoint> nuggets = {{3, 3}, {10, 2}, {5, 9}, {11, 8}, {8, 12}};
    for (const QPoint &point : nuggets) {
        painter.setBrush(gray(0.98));
        painter.drawRect(point.x(), point.y(), 2, 2);
        painter.setBrush(gray(0.86));
        painter.drawRect(point.x() + 1, point.y() + 1, 1, 1);
        painter.setBrush(gray(0.62));
        painter.drawPoint(point.x(), point.y() + 1);
    }
    painter.end();
    return image;
}

QImage TemplateLibrary::makeBlock()
{
    QImage image = makeCanvas();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QLinearGradient gradient(0, 0, 16, 16);
    gradient.setColorAt(0.0, gray(0.88));
    gradient.setColorAt(0.45, gray(0.66));
    gradient.setColorAt(1.0, gray(0.36));
    painter.setPen(QPen(gray(0.22), 1));
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRect(1, 1, 14, 14), 1, 1);

    painter.setPen(QPen(gray(1.0, 190), 1));
    painter.drawLine(2, 2, 13, 2);
    painter.drawLine(2, 2, 2, 13);
    painter.setPen(QPen(gray(0.18), 1));
    painter.drawLine(3, 14, 14, 14);
    painter.drawLine(14, 3, 14, 14);
    painter.setPen(QPen(gray(0.52), 1));
    painter.drawRect(4, 4, 8, 8);
    painter.end();
    return image;
}

QImage TemplateLibrary::makeGem()
{
    QImage image = makeCanvas();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const QPolygon crown({QPoint(8, 1), QPoint(13, 6), QPoint(8, 9), QPoint(3, 6)});
    QLinearGradient gradient(0, 1, 0, 14);
    gradient.setColorAt(0.0, gray(1.0));
    gradient.setColorAt(0.4, gray(0.78));
    gradient.setColorAt(1.0, gray(0.45));
    painter.setPen(QPen(gray(0.20), 1));
    painter.setBrush(gradient);
    painter.drawPolygon(crown);

    const QPolygon pavilion({QPoint(3, 6), QPoint(8, 9), QPoint(8, 15)});
    painter.setBrush(gray(0.64));
    painter.drawPolygon(pavilion);
    const QPolygon pavilion2({QPoint(13, 6), QPoint(8, 9), QPoint(8, 15)});
    painter.setBrush(gray(0.52));
    painter.drawPolygon(pavilion2);

    painter.setPen(QPen(gray(0.90), 1));
    painter.drawLine(5, 5, 8, 3);
    painter.drawLine(8, 3, 11, 5);
    painter.setPen(QPen(gray(0.30), 1));
    painter.drawLine(8, 9, 8, 14);
    painter.end();
    return image;
}

QImage TemplateLibrary::makeDust()
{
    QImage image = makeCanvas();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    painter.setPen(Qt::NoPen);
    const QVector<QPoint> pile = {{4, 12}, {5, 11}, {6, 10}, {7, 10}, {8, 11},
                                  {9, 12}, {6, 12}, {7, 12}, {8, 12}, {7, 9},
                                  {5, 12}, {9, 10}};
    for (const QPoint &point : pile) {
        painter.setBrush(gray(0.82));
        painter.drawRect(point.x(), point.y(), 2, 2);
        painter.setBrush(gray(1.0));
        painter.drawPoint(point.x(), point.y());
        painter.setBrush(gray(0.48));
        painter.drawPoint(point.x() + 1, point.y() + 1);
    }
    painter.setPen(QPen(gray(0.28), 1));
    painter.drawLine(3, 14, 13, 14);
    painter.end();
    return image;
}

QImage TemplateLibrary::makeHelmet()
{
    QImage image = makeCanvas();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QLinearGradient gradient(0, 2, 0, 14);
    gradient.setColorAt(0.0, gray(0.95));
    gradient.setColorAt(0.55, gray(0.70));
    gradient.setColorAt(1.0, gray(0.38));
    painter.setPen(QPen(gray(0.20), 1));
    painter.setBrush(gradient);
    painter.drawRoundedRect(QRect(3, 2, 10, 9), 3, 3);
    painter.drawRect(2, 6, 12, 5);
    painter.drawRect(4, 11, 3, 3);
    painter.drawRect(9, 11, 3, 3);

    painter.setBrush(gray(0.25));
    painter.setPen(Qt::NoPen);
    painter.drawRect(4, 7, 8, 2);
    painter.setBrush(gray(1.0, 200));
    painter.drawRect(4, 3, 5, 1);
    painter.end();
    return image;
}

QImage TemplateLibrary::makeChestplate()
{
    QImage image = makeCanvas();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QLinearGradient gradient(0, 1, 0, 15);
    gradient.setColorAt(0.0, gray(0.95));
    gradient.setColorAt(0.5, gray(0.72));
    gradient.setColorAt(1.0, gray(0.40));
    painter.setPen(QPen(gray(0.20), 1));
    painter.setBrush(gradient);
    const QPolygon body({QPoint(5, 2), QPoint(11, 2), QPoint(14, 5), QPoint(13, 13),
                         QPoint(10, 13), QPoint(10, 15), QPoint(6, 15), QPoint(6, 13),
                         QPoint(3, 13), QPoint(2, 5)});
    painter.drawPolygon(body);

    painter.setBrush(gray(0.24));
    painter.setPen(Qt::NoPen);
    painter.drawRect(6, 2, 4, 2);
    painter.setPen(QPen(gray(0.95, 190), 1));
    painter.drawLine(5, 4, 5, 12);
    painter.drawLine(5, 4, 9, 4);
    painter.setPen(QPen(gray(0.30), 1));
    painter.drawLine(8, 6, 8, 14);
    painter.end();
    return image;
}

QImage TemplateLibrary::makeLeggings()
{
    QImage image = makeCanvas();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QLinearGradient gradient(0, 1, 0, 15);
    gradient.setColorAt(0.0, gray(0.92));
    gradient.setColorAt(0.5, gray(0.68));
    gradient.setColorAt(1.0, gray(0.36));
    painter.setPen(QPen(gray(0.20), 1));
    painter.setBrush(gradient);
    const QPolygon shape({QPoint(4, 2), QPoint(12, 2), QPoint(12, 6), QPoint(10, 6),
                          QPoint(10, 15), QPoint(7, 15), QPoint(7, 7), QPoint(6, 7),
                          QPoint(6, 15), QPoint(4, 15), QPoint(4, 6)});
    painter.drawPolygon(shape);

    painter.setPen(QPen(gray(0.95, 190), 1));
    painter.drawLine(5, 3, 11, 3);
    painter.setPen(QPen(gray(0.30), 1));
    painter.drawLine(7, 8, 7, 14);
    painter.end();
    return image;
}

QImage TemplateLibrary::makeBoots()
{
    QImage image = makeCanvas();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QLinearGradient gradient(0, 3, 0, 14);
    gradient.setColorAt(0.0, gray(0.92));
    gradient.setColorAt(0.6, gray(0.66));
    gradient.setColorAt(1.0, gray(0.34));
    painter.setPen(QPen(gray(0.20), 1));
    painter.setBrush(gradient);
    const QPolygon left({QPoint(3, 4), QPoint(7, 4), QPoint(7, 11), QPoint(5, 13),
                         QPoint(2, 13), QPoint(2, 11), QPoint(3, 10)});
    const QPolygon right({QPoint(9, 4), QPoint(13, 4), QPoint(13, 10), QPoint(14, 11),
                          QPoint(14, 13), QPoint(11, 13), QPoint(9, 11)});
    painter.drawPolygon(left);
    painter.drawPolygon(right);

    painter.setPen(QPen(gray(1.0, 190), 1));
    painter.drawLine(4, 5, 6, 5);
    painter.drawLine(10, 5, 12, 5);
    painter.setPen(QPen(gray(0.28), 1));
    painter.drawLine(3, 12, 6, 12);
    painter.drawLine(10, 12, 13, 12);
    painter.end();
    return image;
}

QImage TemplateLibrary::makeSword()
{
    QImage image = makeCanvas();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);

    QLinearGradient blade(4, 12, 12, 2);
    blade.setColorAt(0.0, gray(0.50));
    blade.setColorAt(0.5, gray(0.90));
    blade.setColorAt(1.0, gray(0.70));
    painter.setPen(QPen(gray(0.18), 1));
    painter.setBrush(blade);
    const QPolygon bladeShape({QPoint(4, 12), QPoint(11, 2), QPoint(13, 2), QPoint(13, 4),
                               QPoint(6, 13)});
    painter.drawPolygon(bladeShape);

    painter.setBrush(gray(0.38));
    painter.drawRect(3, 10, 5, 2);
    painter.drawRect(2, 12, 2, 3);
    painter.setPen(QPen(gray(0.95), 1));
    painter.drawLine(5, 11, 12, 3);
    painter.end();
    return image;
}

QVector<TemplateEntry> TemplateLibrary::builtinTemplates()
{
    struct Builtin {
        const char *key;
        const char *name;
        QImage (*factory)();
    };
    const Builtin items[] = {
        {"ingot", "锭（通用）", &TemplateLibrary::makeIngot},
        {"ore", "矿块", &TemplateLibrary::makeOre},
        {"block", "金属块", &TemplateLibrary::makeBlock},
        {"gem", "宝石", &TemplateLibrary::makeGem},
        {"dust", "粉", &TemplateLibrary::makeDust},
        {"helmet", "头盔", &TemplateLibrary::makeHelmet},
        {"chestplate", "胸甲", &TemplateLibrary::makeChestplate},
        {"leggings", "护腿", &TemplateLibrary::makeLeggings},
        {"boots", "靴子", &TemplateLibrary::makeBoots},
        {"sword", "剑", &TemplateLibrary::makeSword},
    };

    QVector<TemplateEntry> result;
    for (const Builtin &item : items) {
        TemplateEntry entry;
        entry.id = QStringLiteral("builtin:") + QString::fromLatin1(item.key);
        entry.name = QString::fromUtf8(item.name);
        entry.pack = QStringLiteral("内置占位");
        entry.builtin = true;
        entry.builtinImage = item.factory();
        result.append(entry);
    }
    return result;
}

QVector<TemplateEntry> TemplateLibrary::entries() const
{
    QVector<TemplateEntry> result = builtinTemplates();
    QString error;
    if (!ensureRoot(&error))
        return result;

    ensureBuiltinEquipmentPack();

    QDirIterator iterator(root(), {QStringLiteral("*.zip"), QStringLiteral("*.png")},
                          QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QFileInfo info(path);
        if (info.suffix().compare(QLatin1String("zip"), Qt::CaseInsensitive) == 0) {
            ZipArchive archive(path);
            if (!archive.isOpen())
                continue;
            const QString packName = info.completeBaseName();
            for (const ZipEntry &zipEntry : archive.entries()) {
                if (zipEntry.isDir
                    || !zipEntry.path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)) {
                    continue;
                }
                TemplateEntry entry;
                entry.id = QStringLiteral("zip:%1:%2").arg(path, zipEntry.path);
                entry.name = QFileInfo(zipEntry.path).completeBaseName();
                entry.pack = packName;
                entry.zipPath = path;
                entry.entryPath = zipEntry.path;
                result.append(entry);
            }
        } else if (info.suffix().compare(QLatin1String("png"), Qt::CaseInsensitive) == 0) {
            TemplateEntry entry;
            entry.id = QStringLiteral("file:%1").arg(path);
            entry.name = info.completeBaseName();
            entry.pack = QStringLiteral("导入的 PNG");
            entry.filePath = path;
            result.append(entry);
        }
    }
    return result;
}

QImage TemplateLibrary::load(const TemplateEntry &entry) const
{
    if (entry.builtin)
        return entry.builtinImage;
    if (!entry.zipPath.isEmpty()) {
        ZipArchive archive(entry.zipPath);
        if (!archive.isOpen())
            return {};
        return QImage::fromData(archive.read(entry.entryPath));
    }
    if (!entry.filePath.isEmpty())
        return QImage(entry.filePath);
    return {};
}

QVector<TemplateEntry> TemplateLibrary::packAssets(const QString &zipPath) const
{
    QVector<TemplateEntry> result;
    ZipArchive archive(zipPath);
    if (!archive.isOpen())
        return result;
    const QString packName = QFileInfo(zipPath).completeBaseName();
    for (const ZipEntry &zipEntry : archive.entries()) {
        if (zipEntry.isDir
            || !zipEntry.path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)
            || !zipEntry.path.startsWith(QLatin1String("assets/"), Qt::CaseInsensitive)) {
            continue;
        }
        TemplateEntry entry;
        entry.id = QStringLiteral("asset:%1:%2").arg(zipPath, zipEntry.path);
        entry.name = QFileInfo(zipEntry.path).completeBaseName();
        entry.pack = packName;
        entry.zipPath = zipPath;
        entry.entryPath = zipEntry.path;
        result.append(entry);
    }
    return result;
}

QString TemplateLibrary::ensureBuiltinEquipmentPack() const
{
    QString error;
    if (!ensureRoot(&error))
        return {};
    const QString path =
        QDir(root()).filePath(QStringLiteral("builtin-equipment.zip"));
    if (QFileInfo::exists(path) && QFileInfo(path).size() > 0)
        return path;

    const QVector<TemplateEntry> builtins = builtinTemplates();
    struct Item {
        const char *iconName;
        const char *target;
    };
    const Item items[] = {
        {"头盔", "assets/minecraft/textures/item/helmet.png"},
        {"胸甲", "assets/minecraft/textures/item/chestplate.png"},
        {"护腿", "assets/minecraft/textures/item/leggings.png"},
        {"靴子", "assets/minecraft/textures/item/boots.png"},
    };

    QZipWriter writer(path);
    if (writer.status() != QZipWriter::NoError)
        return {};
    for (const Item &item : items) {
        QImage image;
        for (const TemplateEntry &entry : builtins) {
            if (entry.name.contains(QString::fromUtf8(item.iconName))) {
                image = entry.builtinImage;
                break;
            }
        }
        if (image.isNull())
            continue;
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        writer.addFile(QString::fromUtf8(item.target), bytes);
    }
    writer.close();
    return path;
}

bool TemplateLibrary::importZip(const QString &zipPath, QString *error) const
{
    if (!ensureRoot(error))
        return false;
    ZipArchive archive(zipPath);
    if (!archive.isOpen()) {
        if (error)
            *error = QStringLiteral("无法读取模板包：%1").arg(zipPath);
        return false;
    }
    bool hasPng = false;
    for (const ZipEntry &entry : archive.entries()) {
        if (!entry.isDir
            && entry.path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)) {
            hasPng = true;
            break;
        }
    }
    if (!hasPng) {
        if (error)
            *error = QStringLiteral("这个 ZIP 里没有 PNG 模板。");
        return false;
    }

    const QString target =
        uniqueTarget(root(), QFileInfo(zipPath).fileName());
    if (!QFile::copy(zipPath, target)) {
        if (error)
            *error = QStringLiteral("无法复制模板包到：%1").arg(target);
        return false;
    }
    return true;
}

bool TemplateLibrary::importImage(const QImage &image, const QString &name,
                                  QString *error) const
{
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("模板图片为空。");
        return false;
    }
    if (!ensureRoot(error))
        return false;

    QString base = name;
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]+")),
                 QStringLiteral("_"));
    base = base.trimmed();
    if (base.isEmpty())
        base = QStringLiteral("template");

    const QString target =
        uniqueTarget(root(), base + QStringLiteral(".png"));
    if (!image.save(target, "PNG")) {
        if (error)
            *error = QStringLiteral("无法写入模板：%1").arg(target);
        return false;
    }
    return true;
}

} // namespace mcstudio
