#pragma once

#include <QImage>
#include <QString>
#include <QVector>

namespace mcstudio {

struct TemplateEntry {
    QString id;
    QString name;
    QString pack;
    QString zipPath;
    QString entryPath;
    QString filePath;
    QImage builtinImage;
    bool builtin = false;
    bool colored = false;
};

// Template packs are ZIP files containing PNGs (or loose PNGs in the templates
// folder). A short built-in set of original grayscale templates is generated in
// code so the feature works without shipping any game textures.
class TemplateLibrary
{
public:
    static QString root();
    static bool ensureRoot(QString *error = nullptr);

    QVector<TemplateEntry> entries() const;
    QImage load(const TemplateEntry &entry) const;
    QVector<TemplateEntry> packAssets(const QString &zipPath) const;
    QString ensureBuiltinEquipmentPack() const;

    bool importZip(const QString &zipPath, QString *error = nullptr) const;
    bool importImage(const QImage &image, const QString &name,
                     QString *error = nullptr) const;

    static QVector<TemplateEntry> builtinTemplates();

private:
    static QImage makeIngot();
    static QImage makeOre();
    static QImage makeBlock();
    static QImage makeGem();
    static QImage makeDust();
    static QImage makeHelmet();
    static QImage makeChestplate();
    static QImage makeLeggings();
    static QImage makeBoots();
    static QImage makeSword();
};

} // namespace mcstudio
