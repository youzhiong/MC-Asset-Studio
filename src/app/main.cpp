#include "core/AppPaths.h"
#include "core/AssetNamespace.h"
#include "core/GameEnvironment.h"
#include "core/GameSettings.h"
#include "core/ModCatalog.h"
#include "core/ModScanner.h"
#include "core/ProjectService.h"
#include "core/Version.h"
#include "core/ZipArchive.h"
#include "core/AssociationTask.h"
#include "ui/CoverEditorDialog.h"
#include "ui/IconFactory.h"
#include "ui/MainWindow.h"
#include "ui/NewProjectDialog.h"
#include "ui/OpenProjectDialog.h"
#include "ui/PreferencesDialog.h"
#include "ui/ResourcePackImportDialog.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QWidget>

#include <utility>

namespace {

QString formatGameReport(const mcstudio::GameEnvironmentSnapshot &snapshot)
{
    QString text;
    QTextStream stream(&text);
    stream << "MC Asset Studio environment report\n";
    stream << "schema=1\n";
    stream << "roots=" << snapshot.roots.size()
           << " instances=" << snapshot.instanceCount()
           << " mods=" << snapshot.modCount()
           << " resourcepacks=" << snapshot.resourcePackCount() << '\n';

    for (const mcstudio::GameRootInfo &root : snapshot.roots) {
        stream << "\n[root]\n";
        stream << "path=" << root.path << '\n';
        stream << "key=" << root.key << '\n';
        stream << "instances=" << root.instanceCount() << '\n';
        stream << "rootMods=" << (root.hasRootMods ? "yes" : "no")
               << " rootResourcePacks=" << (root.hasRootResourcePacks ? "yes" : "no")
               << " assets=" << (root.hasAssets ? "yes" : "no")
               << " libraries=" << (root.hasLibraries ? "yes" : "no") << '\n';
        stream << "fromCache=" << (root.fromCache ? "yes" : "no") << '\n';
        for (const QString &warning : root.warnings)
            stream << "warning=" << warning << '\n';

        for (const mcstudio::InstanceInfo &instance : root.instances) {
            stream << "  [instance]\n";
            stream << "  key=" << instance.key << '\n';
            stream << "  name=" << instance.name << '\n';
            stream << "  clientVersion=" << instance.versionLabel() << '\n';
            stream << "  loader=" << instance.loader << " version=" << instance.loaderVersion
                   << '\n';
            stream << "  assetIndex=" << instance.assetIndex << '\n';
            stream << "  isolated=" << (instance.isolated ? "yes" : "no") << '\n';
            stream << "  clientJar=" << instance.clientJar << '\n';
            stream << "  hasClientJar=" << (instance.hasClientJar ? "yes" : "no") << '\n';
            stream << "  mods=" << instance.modCount()
                   << " rootMods=" << instance.rootModCount()
                   << " instanceMods=" << instance.instanceModCount()
                   << " disabled=" << instance.disabledModCount() << '\n';
            stream << "  resourcepacks=" << instance.resourcePackCount() << '\n';
            for (const QString &warning : instance.warnings)
                stream << "  warning=" << warning << '\n';
        }
    }

    if (!snapshot.warnings.isEmpty()) {
        stream << "\n[snapshot]\n";
        for (const QString &warning : snapshot.warnings)
            stream << "warning=" << warning << '\n';
    }
    return text;
}

bool writeTextFile(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write(text.toUtf8());
    return file.commit();
}

QStringList namespacesFromPackZip(const QString &zipPath, int *packFormat, QString *error)
{
    *packFormat = 0;
    mcstudio::ZipArchive archive(zipPath);
    if (!archive.isOpen()) {
        if (error)
            *error = archive.error();
        return {};
    }

    const QList<mcstudio::ZipEntry> entries = archive.entries();
    QStringList paths;
    QString metaPath;
    for (const mcstudio::ZipEntry &entry : entries) {
        if (entry.isDir)
            continue;
        paths.append(entry.path);
        const QString normalized = QDir::fromNativeSeparators(entry.path);
        if (normalized == QLatin1String("pack.mcmeta")) {
            metaPath = entry.path;
        } else if (normalized.endsWith(QLatin1String("/pack.mcmeta"))) {
            if (metaPath.isEmpty() || normalized.size() < metaPath.size())
                metaPath = entry.path;
        }
    }

    if (!metaPath.isEmpty()) {
        QByteArray data = archive.read(metaPath);
        static const char kUtf8Bom[] = "\xEF\xBB\xBF";
        if (data.startsWith(kUtf8Bom))
            data.remove(0, 3);
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            *packFormat = document.object()
                              .value(QStringLiteral("pack"))
                              .toObject()
                              .value(QStringLiteral("pack_format"))
                              .toInt();
        }
    }

    const QStringList namespaces = mcstudio::namespacesFromPaths(paths);
    if (namespaces.isEmpty() && error)
        *error = QStringLiteral("资源包内没有 assets/<namespace>/ 目录");
    return namespaces;
}

const mcstudio::InstanceInfo *chooseReferenceInstance(
    const mcstudio::GameEnvironmentSnapshot &snapshot,
    const mcstudio::GameSettings &settings, const QString &filter, QString *reason)
{
    if (!filter.isEmpty()) {
        for (const mcstudio::GameRootInfo &root : snapshot.roots) {
            for (const mcstudio::InstanceInfo &instance : root.instances) {
                if (instance.name.contains(filter, Qt::CaseInsensitive)
                    || instance.key.contains(filter, Qt::CaseInsensitive)) {
                    if (reason)
                        *reason = QStringLiteral("匹配 --mod-instance");
                    return &instance;
                }
            }
        }
        return nullptr;
    }

    if (!settings.defaultInstanceKey.isEmpty()) {
        if (const mcstudio::InstanceInfo *instance =
                snapshot.findInstance(settings.defaultInstanceKey)) {
            if (reason)
                *reason = QStringLiteral("首选项默认实例");
            return instance;
        }
    }

    const mcstudio::InstanceInfo *best = nullptr;
    for (const mcstudio::GameRootInfo &root : snapshot.roots) {
        for (const mcstudio::InstanceInfo &instance : root.instances) {
            if (!best || instance.modCount() > best->modCount())
                best = &instance;
        }
    }
    if (best && reason)
        *reason = QStringLiteral("mod 数最多的实例");
    return best;
}

QString formatModReport(const mcstudio::ModCatalog &catalog,
                        const mcstudio::InstanceInfo &reference,
                        const QStringList &namespaces, int packFormat,
                        const QString &selectionReason, qint64 elapsedMs)
{
    QString text;
    QTextStream stream(&text);
    stream << "MC Asset Studio mod namespace report\n";
    stream << "schema=1\n";
    stream << "referenceKey=" << reference.key << '\n';
    stream << "referenceName=" << reference.name << '\n';
    stream << "clientVersion=" << reference.versionLabel() << '\n';
    stream << "loader=" << reference.loader << ' ' << reference.loaderVersion << '\n';
    stream << "selection=" << selectionReason << '\n';
    stream << "packFormat=" << packFormat << '\n';
    stream << "namespaces=" << namespaces.size() << '\n';
    stream << "localJars=" << reference.modCount() << '\n';
    stream << "jarScans=" << catalog.jarScanCount() << '\n';
    stream << "cachedJars=" << catalog.cachedJarCount() << '\n';
    stream << "localMods=" << catalog.localMods().size() << '\n';
    stream << "crossInstances=" << catalog.crossInstancesScanned().join(QLatin1Char(',')) << '\n';
    stream << "found=" << catalog.foundCount() << " missing=" << catalog.missingCount()
           << " cross=" << catalog.crossCount() << " builtin=" << catalog.builtinCount() << '\n';
    stream << "elapsedMs=" << elapsedMs << '\n';

    for (const mcstudio::NamespaceMod &entry : catalog.entries()) {
        stream << "\n[namespace]\n";
        stream << "namespace=" << entry.namespaceId << '\n';
        stream << "modId=" << entry.modId << '\n';
        stream << "display=" << entry.label() << '\n';
        stream << "version=" << entry.version << '\n';
        stream << "source=" << entry.source << '\n';
        stream << "sourceInstance=" << entry.sourceInstanceName << '\n';
        stream << "jar=" << entry.jarPath << '\n';
        stream << "logo=" << entry.logoFile << '\n';
        stream << "assets=" << entry.assetCount << '\n';
        stream << "ambiguous=" << (entry.ambiguous ? "yes" : "no") << '\n';
        stream << "detail=" << entry.detail << '\n';
        if (!entry.candidateJars.isEmpty())
            stream << "candidates=" << entry.candidateJars.size() << '\n';
    }

    const QVector<QString> &errors = catalog.errors();
    if (!errors.isEmpty()) {
        stream << "\n[errors]\n";
        for (const QString &error : errors)
            stream << "error=" << error << '\n';
    }
    return text;
}


} // namespace

int main(int argc, char *argv[])
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MCStudio"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("mcstudio.local"));
    QCoreApplication::setApplicationName(QStringLiteral("MC Asset Studio"));
    QCoreApplication::setApplicationVersion(mcstudio::version());

    QApplication::setWindowIcon(IconFactory::appIcon());

    // Copy old portable data/ into the per-user AppData and Documents layout
    // on first launch. The old folder is intentionally left untouched.
    const QString migration = mcstudio::AppPaths::migrateLegacyData();
    if (!migration.isEmpty()) {
        if (migration.startsWith(QStringLiteral("已把旧数据")))
            qInfo("%s", qUtf8Printable(migration));
        else
            qWarning("%s", qUtf8Printable(migration));
    }

    const mcstudio::GameSettings startupSettings = mcstudio::loadGameSettings();
    Theme::apply(app, startupSettings.theme, startupSettings.widgetStyle);
    Theme::installSystemThemeWatcher();

    QString screenshotPath;
    QString importPath;
    QString importPath2;
    QString exportPath;
    QString tabKey;
    QString categoryKey;
    QString texturePath;
    QString collectionShotPath;
    QString gameRoot;
    QString gameReportPath;
    QString modReportPath;
    QString modPackPath;
    QString modInstanceFilter;
    QString modNamespace;
    QString associationReportPath;
    QString diagnosticsReportPath;
    QString referenceInstance;
    QString collectionMode;
    QString resourcePackShotPath;
    QString importInstancePack;
    QString searchText;
    QString newProjectName;
    QString openProjectDir;
    QString importFolderPath;
    QString newProjectShotPath;
    QString openProjectShotPath;
    QString coverImagePath;
    QString coverEditorShotPath;
    QString packEditorNamespace;
    QString packEditorCover;
    QString importReferencePath;
    QString importReferenceNamespace;
    QString editTexturePath;
    QString editorReferencePath;
    QString editorShotPath;
    QString dialogShotPath;
    bool editorPopout = false;
    bool editorReturn = false;
    bool editorFocus = false;
    bool showTemplateDialog = false;
    bool showTintDialog = false;
    bool showReduceDialog = false;
    bool applyTintDemo = false;
    bool applyHalfMask = false;
    bool applyHalfMaskTint = false;
    bool showEquipmentDialog = false;
    bool showMergeDialog = false;
    bool generateEquipment = false;
    bool saveEditor = false;
    int packMetaFormat = -1;
    QString packMetaDescription;
    QString preferencesShotPath;    int packIndex = -1;
    int preferencesTab = -1;
    int screenshotDelay = 2400;
    int collectionShotDelay = 2800;
    bool overwrite = false;
    bool showCollection = false;
    bool associationMode = false;
    bool showPackEditor = false;
    bool showPackEditorAll = false;
    bool showReferenceTextures = false;
    bool collectionFit = false;
    bool gameRefresh = false;
    bool modRefresh = false;

    const QStringList args = QCoreApplication::arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString arg = args.at(i);
        if (arg == QLatin1String("--screenshot") && i + 1 < args.size())
            screenshotPath = args.at(++i);
        else if (arg == QLatin1String("--import") && i + 1 < args.size())
            importPath = args.at(++i);
        else if (arg == QLatin1String("--import2") && i + 1 < args.size())
            importPath2 = args.at(++i);
        else if (arg == QLatin1String("--export") && i + 1 < args.size())
            exportPath = args.at(++i);
        else if (arg == QLatin1String("--tab") && i + 1 < args.size())
            tabKey = args.at(++i);
        else if (arg == QLatin1String("--category") && i + 1 < args.size())
            categoryKey = args.at(++i);
        else if (arg == QLatin1String("--texture") && i + 1 < args.size())
            texturePath = args.at(++i);
        else if (arg == QLatin1String("--pack") && i + 1 < args.size())
            packIndex = args.at(++i).toInt();
        else if (arg == QLatin1String("--collection"))
            showCollection = true;
        else if (arg == QLatin1String("--collection-assoc")) {
            showCollection = true;
            associationMode = true;
        } else if (arg == QLatin1String("--collection-mode") && i + 1 < args.size()) {
            showCollection = true;
            collectionMode = args.at(++i);
        } else if (arg == QLatin1String("--collection-shot") && i + 1 < args.size()) {
            showCollection = true;
            collectionShotPath = args.at(++i);
        } else if (arg == QLatin1String("--game-root") && i + 1 < args.size())
            gameRoot = args.at(++i);
        else if (arg == QLatin1String("--game-report") && i + 1 < args.size())
            gameReportPath = args.at(++i);
        else if (arg == QLatin1String("--mod-report") && i + 1 < args.size())
            modReportPath = args.at(++i);
        else if (arg == QLatin1String("--mod-pack") && i + 1 < args.size())
            modPackPath = args.at(++i);
        else if (arg == QLatin1String("--mod-instance") && i + 1 < args.size())
            modInstanceFilter = args.at(++i);
        else if (arg == QLatin1String("--mod") && i + 1 < args.size())
            modNamespace = args.at(++i);
        else if (arg == QLatin1String("--reference") && i + 1 < args.size())
            referenceInstance = args.at(++i);
        else if (arg == QLatin1String("--association-report") && i + 1 < args.size())
            associationReportPath = args.at(++i);
        else if (arg == QLatin1String("--diagnostics-report") && i + 1 < args.size())
            diagnosticsReportPath = args.at(++i);
        else if (arg == QLatin1String("--import-instance") && i + 1 < args.size())
            importInstancePack = args.at(++i);
        else if (arg == QLatin1String("--search") && i + 1 < args.size())
            searchText = args.at(++i);
        else if (arg == QLatin1String("--resourcepack-shot") && i + 1 < args.size())
            resourcePackShotPath = args.at(++i);
        else if (arg == QLatin1String("--new-project") && i + 1 < args.size())
            newProjectName = args.at(++i);
        else if (arg == QLatin1String("--open-project") && i + 1 < args.size())
            openProjectDir = args.at(++i);
        else if (arg == QLatin1String("--import-folder") && i + 1 < args.size())
            importFolderPath = args.at(++i);
        else if (arg == QLatin1String("--pack-editor"))
            showPackEditor = true;
        else if (arg == QLatin1String("--pack-editor-all"))
            showPackEditorAll = true;
        else if (arg == QLatin1String("--show-reference"))
            showReferenceTextures = true;
        else if (arg == QLatin1String("--collection-fit"))
            collectionFit = true;
        else if (arg == QLatin1String("--import-reference") && i + 1 < args.size())
            importReferencePath = args.at(++i);
        else if (arg == QLatin1String("--import-reference-namespace") && i + 1 < args.size())
            importReferenceNamespace = args.at(++i);
        else if (arg == QLatin1String("--edit-texture") && i + 1 < args.size())
            editTexturePath = args.at(++i);
        else if (arg == QLatin1String("--editor-reference") && i + 1 < args.size())
            editorReferencePath = args.at(++i);
        else if (arg == QLatin1String("--editor-shot") && i + 1 < args.size())
            editorShotPath = args.at(++i);
        else if (arg == QLatin1String("--dialog-shot") && i + 1 < args.size())
            dialogShotPath = args.at(++i);
        else if (arg == QLatin1String("--template-dialog"))
            showTemplateDialog = true;
        else if (arg == QLatin1String("--tint-dialog"))
            showTintDialog = true;
        else if (arg == QLatin1String("--reduce-dialog"))
            showReduceDialog = true;
        else if (arg == QLatin1String("--editor-tint-demo"))
            applyTintDemo = true;
        else if (arg == QLatin1String("--editor-half-mask"))
            applyHalfMask = true;
        else if (arg == QLatin1String("--editor-half-mask-tint"))
            applyHalfMaskTint = true;
        else if (arg == QLatin1String("--equipment-dialog"))
            showEquipmentDialog = true;
        else if (arg == QLatin1String("--merge-dialog"))
            showMergeDialog = true;
        else if (arg == QLatin1String("--equipment-demo"))
            generateEquipment = true;
        else if (arg == QLatin1String("--editor-save"))
            saveEditor = true;
        else if (arg == QLatin1String("--editor-popout"))
            editorPopout = true;
        else if (arg == QLatin1String("--editor-return"))
            editorReturn = true;
        else if (arg == QLatin1String("--editor-focus"))
            editorFocus = true;
        else if (arg == QLatin1String("--new-project-shot") && i + 1 < args.size())
            newProjectShotPath = args.at(++i);
        else if (arg == QLatin1String("--open-project-shot") && i + 1 < args.size())
            openProjectShotPath = args.at(++i);
        else if (arg == QLatin1String("--cover-image") && i + 1 < args.size())
            coverImagePath = args.at(++i);
        else if (arg == QLatin1String("--cover-editor-shot") && i + 1 < args.size())
            coverEditorShotPath = args.at(++i);
        else if (arg == QLatin1String("--pack-editor-namespace") && i + 1 < args.size())
            packEditorNamespace = args.at(++i);
        else if (arg == QLatin1String("--pack-editor-cover") && i + 1 < args.size())
            packEditorCover = args.at(++i);
        else if (arg == QLatin1String("--packmeta-format") && i + 1 < args.size())
            packMetaFormat = args.at(++i).toInt();
        else if (arg == QLatin1String("--packmeta-description") && i + 1 < args.size())
            packMetaDescription = args.at(++i);
        else if (arg == QLatin1String("--mod-refresh"))
            modRefresh = true;
        else if (arg == QLatin1String("--preferences-shot") && i + 1 < args.size())
            preferencesShotPath = args.at(++i);
        else if (arg == QLatin1String("--preferences-tab") && i + 1 < args.size())
            preferencesTab = args.at(++i).toInt();
        else if (arg == QLatin1String("--screenshot-delay") && i + 1 < args.size())
            screenshotDelay = qMax(100, args.at(++i).toInt());
        else if (arg == QLatin1String("--collection-shot-delay") && i + 1 < args.size())
            collectionShotDelay = qMax(100, args.at(++i).toInt());
        else if (arg == QLatin1String("--game-refresh"))
            gameRefresh = true;
        else if (arg == QLatin1String("--overwrite"))
            overwrite = true;
    }

    if (!gameRoot.isEmpty()) {
        mcstudio::GameSettings settings = mcstudio::loadGameSettings();
        mcstudio::rememberGameRoot(settings, gameRoot);
        mcstudio::saveGameSettings(settings);
    }

    // Headless environment scan used by packaging / acceptance checks.
    if (!gameReportPath.isEmpty()) {
        const mcstudio::GameSettings settings = mcstudio::loadGameSettings();
        if (settings.roots.isEmpty()) {
            qWarning("No game roots configured. Pass --game-root first.");
            return 2;
        }
        mcstudio::GameScanOptions options;
        options.forceRefresh = gameRefresh;
        const mcstudio::GameEnvironmentSnapshot snapshot =
            mcstudio::GameEnvironment::scanRoots(settings.roots, options);
        if (!writeTextFile(gameReportPath, formatGameReport(snapshot))) {
            qWarning("Could not write environment report to %s", qPrintable(gameReportPath));
            return 3;
        }
        return 0;
    }

    // Headless mod metadata / namespace mapping report (phase 2 acceptance).
    if (!modReportPath.isEmpty()) {
        const mcstudio::GameSettings settings = mcstudio::loadGameSettings();
        if (settings.roots.isEmpty()) {
            qWarning("No game roots configured. Pass --game-root first.");
            return 2;
        }

        mcstudio::GameScanOptions scanOptions;
        scanOptions.forceRefresh = modRefresh;
        const mcstudio::GameEnvironmentSnapshot snapshot =
            mcstudio::GameEnvironment::scanRoots(settings.roots, scanOptions);

        QString selectionReason;
        const mcstudio::InstanceInfo *reference =
            chooseReferenceInstance(snapshot, settings, modInstanceFilter, &selectionReason);
        if (!reference) {
            qWarning("No matching instance for the mod report.");
            return 4;
        }

        int packFormat = 0;
        QStringList namespaces;
        if (!modPackPath.isEmpty()) {
            QString packError;
            namespaces = namespacesFromPackZip(modPackPath, &packFormat, &packError);
            if (namespaces.isEmpty()) {
                qWarning("Could not read pack namespaces: %s", qPrintable(packError));
                return 5;
            }
        } else {
            QSet<QString> namespaceSet;
            for (const mcstudio::ModFileInfo &jar : reference->mods) {
                if (jar.disabled)
                    continue;
                const mcstudio::JarModScan scan = mcstudio::ModScanner::scanJar(
                    jar, reference->key, reference->loader, modRefresh, true);
                for (const QString &ns : scan.namespaces)
                    namespaceSet.insert(ns);
            }
            for (const QString &ns : std::as_const(namespaceSet))
                namespaces.append(ns);
            namespaces.sort(Qt::CaseInsensitive);
        }

        QVector<mcstudio::InstanceInfo> others;
        for (const mcstudio::GameRootInfo &root : snapshot.roots) {
            for (const mcstudio::InstanceInfo &instance : root.instances) {
                if (instance.key != reference->key)
                    others.append(instance);
            }
        }

        QElapsedTimer timer;
        timer.start();
        const mcstudio::ModCatalog catalog = mcstudio::ModCatalog::build(
            *reference, others, namespaces, true, modRefresh);
        const qint64 elapsed = timer.elapsed();

        if (!writeTextFile(modReportPath,
                           formatModReport(catalog, *reference, namespaces, packFormat,
                                           selectionReason, elapsed))) {
            qWarning("Could not write mod report to %s", qPrintable(modReportPath));
            return 6;
        }
        return 0;
    }

    MainWindow window;
    window.show();

    if (!newProjectShotPath.isEmpty()) {
        auto *dialog = new mcstudio::NewProjectDialog(&window);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        QTimer::singleShot(1600, &app, [dialog, newProjectShotPath] {
            dialog->grab().save(newProjectShotPath);
            QCoreApplication::quit();
        });
    }
    if (!openProjectShotPath.isEmpty()) {
        auto *dialog = new mcstudio::OpenProjectDialog(&window);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        QTimer::singleShot(1800, &app, [dialog, openProjectShotPath] {
            dialog->grab().save(openProjectShotPath);
            QCoreApplication::quit();
        });
    }
    if (!coverEditorShotPath.isEmpty()) {
        QImage image(coverImagePath);
        if (image.isNull()) {
            image = QImage(800, 600, QImage::Format_ARGB32);
            image.fill(QColor(QStringLiteral("#2f7d52")));
        }
        auto *dialog = new mcstudio::CoverEditorDialog(image, 512, &window);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        QTimer::singleShot(1800, &app, [dialog, coverEditorShotPath] {
            dialog->grab().save(coverEditorShotPath);
            QCoreApplication::quit();
        });
    }

    if (!newProjectName.isEmpty()) {
        mcstudio::NewProjectRequest request;
        request.name = newProjectName;
        request.description = newProjectName;
        request.packFormat = 15;
        const mcstudio::ProjectCreateResult result =
            mcstudio::ProjectService::createBlank(mcstudio::AppPaths::projectsRoot(),
                                                  request);
        if (result.ok) {
            QString error;
            if (!window.openProjectDirectory(result.projectDir, &error))
                qWarning("Could not open new project: %s", qPrintable(error));
        } else {
            qWarning("Could not create project: %s", qPrintable(result.error));
        }
    }
    if (!importFolderPath.isEmpty()) {
        const mcstudio::ProjectCreateResult result = mcstudio::ProjectService::importFolder(
            importFolderPath, mcstudio::AppPaths::projectsRoot(),
            QFileInfo(importFolderPath).fileName());
        if (result.ok) {
            QString error;
            if (!window.openProjectDirectory(result.projectDir, &error))
                qWarning("Could not open folder project: %s", qPrintable(error));
        } else {
            qWarning("Could not import folder: %s", qPrintable(result.error));
        }
    }
    if (!openProjectDir.isEmpty()) {
        QString error;
        if (!window.openProjectDirectory(openProjectDir, &error))
            qWarning("Could not open project: %s", qPrintable(error));
    }
    if (!exportPath.isEmpty() && importPath.isEmpty()
        && (!openProjectDir.isEmpty() || !newProjectName.isEmpty())) {
        QString exportError;
        if (!window.exportProject(exportPath, overwrite, &exportError))
            qWarning("Export failed: %s", qPrintable(exportError));
    }
    if (!tabKey.isEmpty() && importPath.isEmpty())
        window.activateTab(tabKey);

    if (!preferencesShotPath.isEmpty()) {
        auto *dialog = new mcstudio::PreferencesDialog(&window);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setCurrentTab(preferencesTab);
        dialog->show();
        QTimer::singleShot(2500, &app, [dialog, preferencesShotPath] {
            if (!dialog->grab().save(preferencesShotPath))
                qWarning("Could not save preferences screenshot to %s",
                         qPrintable(preferencesShotPath));
            QCoreApplication::quit();
        });
    }

    if (!resourcePackShotPath.isEmpty()) {
        auto *dialog = new mcstudio::ResourcePackImportDialog(&window);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
        QTimer::singleShot(1800, &app, [dialog, resourcePackShotPath] {
            if (!dialog->grab().save(resourcePackShotPath))
                qWarning("Could not save resource pack dialog screenshot to %s",
                         qPrintable(resourcePackShotPath));
            QCoreApplication::quit();
        });
    }

    if (!importInstancePack.isEmpty()) {
        const mcstudio::GameSettings settings = mcstudio::loadGameSettings();
        mcstudio::GameEnvironmentSnapshot snapshot =
            mcstudio::GameEnvironment::loadCachedSnapshot(settings.roots);
        if (snapshot.instanceCount() == 0 && !settings.roots.isEmpty())
            snapshot = mcstudio::GameEnvironment::scanRoots(settings.roots, {});

        QString foundPath;
        for (const mcstudio::GameRootInfo &root : snapshot.roots) {
            for (const mcstudio::InstanceInfo &instance : root.instances) {
                for (const mcstudio::ResourcePackFileInfo &pack : instance.resourcePacks) {
                    if (pack.isDirectory
                        || !pack.fileName.endsWith(QLatin1String(".zip"),
                                                   Qt::CaseInsensitive)) {
                        continue;
                    }
                    if (pack.fileName.contains(importInstancePack, Qt::CaseInsensitive)) {
                        foundPath = pack.path;
                        break;
                    }
                }
                if (!foundPath.isEmpty())
                    break;
            }
            if (!foundPath.isEmpty())
                break;
        }

        if (foundPath.isEmpty()) {
            qWarning("No instance resource pack matched %s", qPrintable(importInstancePack));
        } else {
            QString importError;
            if (!window.importZip(foundPath, &importError))
                qWarning("Instance pack import failed: %s", qPrintable(importError));
        }
    }

    if (!importPath.isEmpty()) {
        QString error;
        if (!window.importZip(importPath, &error))
            qWarning("Import failed: %s", qPrintable(error));
        else if (!exportPath.isEmpty()) {
            QString exportError;
            if (!window.exportProject(exportPath, overwrite, &exportError))
                qWarning("Export failed: %s", qPrintable(exportError));
        }
        if (!referenceInstance.isEmpty()) {
            window.selectReferenceInstance(
                referenceInstance.compare(QLatin1String("auto"), Qt::CaseInsensitive) == 0
                    ? QString()
                    : referenceInstance);
        }
        if (!associationReportPath.isEmpty()) {
            window.waitForCatalogReady(30000);
            window.waitForAssociationReady(60000);
            if (!writeTextFile(associationReportPath, window.buildAssociationReport())) {
                qWarning("Could not write association report to %s",
                         qPrintable(associationReportPath));
                return 7;
            }
            return 0;
        }
        if (!diagnosticsReportPath.isEmpty()) {
            window.waitForCatalogReady(30000);
            window.waitForAssociationReady(120000);
            if (!writeTextFile(diagnosticsReportPath, window.buildDiagnosticsReport())) {
                qWarning("Could not write diagnostics report to %s",
                         qPrintable(diagnosticsReportPath));
                return 8;
            }
            return 0;
        }
        if (!tabKey.isEmpty())
            window.activateTab(tabKey);
        if (!categoryKey.isEmpty())
            window.selectCategory(categoryKey);
        if (!modNamespace.isEmpty())
            window.selectMod(modNamespace);
        if (!texturePath.isEmpty())
            window.selectTexture(texturePath);
        if (showCollection)
            window.showCollectionWindow();
        if (associationMode)
            window.setCollectionAssociationMode();
        if (!collectionMode.isEmpty())
            window.setCollectionGroupMode(collectionMode);
    }

    if (packMetaFormat > 0)
        window.applyPackMetaForTesting(packMetaFormat, packMetaDescription);

    if (showPackEditor) {
        window.openPackEditor();
        if (!packEditorNamespace.isEmpty())
            window.applyPackEditorNamespace(packEditorNamespace, true);
        if (!packEditorCover.isEmpty())
            window.applyPackEditorCover(packEditorCover);
    }
    if (showPackEditorAll)
        window.showPackEditorAllForTesting();
    if (showReferenceTextures)
        window.setShowReferenceTextures(true);
    if (!editTexturePath.isEmpty())
        window.openTextureEditor(editTexturePath);
    if (!editorReferencePath.isEmpty())
        window.setTextureEditorReferenceForTesting(editorReferencePath);
    if (applyTintDemo)
        window.applyBuiltinTintForTesting();
    if (applyHalfMask)
        window.applyHalfMaskForTesting();
    if (applyHalfMaskTint)
        window.applyHalfMaskTintForTesting();
    if (generateEquipment)
        window.generateEquipmentForTesting();
    if (showEquipmentDialog) {
        QTimer::singleShot(300, &app, [&window] {
            window.openEquipmentDialogForTesting();
        });
    }
    if (showMergeDialog) {
        QTimer::singleShot(300, &app, [&window] {
            window.openMergeDialogForTesting();
        });
    }
    if (saveEditor)
        window.saveActiveTextureEditorForTesting();
    if (editorPopout)
        window.popOutActiveTextureEditorForTesting();
    if (editorReturn)
        window.returnActiveTextureEditorForTesting();
    if (editorFocus)
        window.setTextureEditorFocusForTesting(true);
    if (!importReferencePath.isEmpty())
        window.importReferencePath(importReferencePath);
    if (!importReferenceNamespace.isEmpty())
        window.importReferenceNamespaceForTesting(importReferenceNamespace);
    if (!texturePath.isEmpty())
        window.selectTexture(texturePath);
    if (importPath.isEmpty() && !categoryKey.isEmpty())
        window.selectCategory(categoryKey);
    if (importPath.isEmpty() && !modNamespace.isEmpty())
        window.selectMod(modNamespace);
    if (importPath.isEmpty() && showCollection)
        window.showCollectionWindow();
    if (importPath.isEmpty() && associationMode)
        window.setCollectionAssociationMode();
    if (importPath.isEmpty() && !collectionMode.isEmpty())
        window.setCollectionGroupMode(collectionMode);
    if (collectionFit)
        window.fitCollectionForTesting();

    if (!searchText.isEmpty())
        window.setSearchText(searchText);

    if (!importPath2.isEmpty()) {
        QString error;
        if (!window.importZip(importPath2, &error))
            qWarning("Second import failed: %s", qPrintable(error));
    }

    if (packIndex >= 0)
        window.selectPackTab(packIndex);

    if (!collectionShotPath.isEmpty()) {
        QTimer::singleShot(collectionShotDelay, &app, [collectionShotPath] {
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                if (widget->windowTitle().startsWith(QStringLiteral("集合模式"))) {
                    widget->grab().save(collectionShotPath);
                    break;
                }
            }
            QCoreApplication::quit();
        });
    }

    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(screenshotDelay, &app, [&window, screenshotPath] {
            if (!window.grab().save(screenshotPath))
                qWarning("Could not save screenshot to %s", qPrintable(screenshotPath));
            QCoreApplication::quit();
        });
    }

    if (!editorShotPath.isEmpty()) {
        QTimer::singleShot(screenshotDelay, &app, [&window, editorShotPath] {
            if (QWidget *editorWindow = window.activeTextureEditorWindowForTesting()) {
                if (!editorWindow->grab().save(editorShotPath))
                    qWarning("Could not save editor screenshot to %s",
                             qPrintable(editorShotPath));
            } else {
                qWarning("No active texture editor window for %s",
                         qPrintable(editorShotPath));
            }
            QCoreApplication::quit();
        });
    }

    if (showTemplateDialog) {
        QTimer::singleShot(300, &app, [&window] {
            window.openTemplateDialogForTesting();
        });
    }
    if (showTintDialog) {
        QTimer::singleShot(300, &app, [&window] {
            window.openTintDialogForTesting();
        });
    }
    if (showReduceDialog) {
        QTimer::singleShot(300, &app, [&window] {
            window.openReduceDialogForTesting();
        });
    }
    if (!dialogShotPath.isEmpty()) {
        QTimer::singleShot(screenshotDelay, &app, [dialogShotPath] {
            QWidget *widget = QApplication::activeModalWidget();
            if (!widget)
                widget = QApplication::activeWindow();
            if (widget && !widget->grab().save(dialogShotPath))
                qWarning("Could not save dialog screenshot to %s",
                         qPrintable(dialogShotPath));
            if (QWidget *modal = QApplication::activeModalWidget())
                modal->close();
            QCoreApplication::quit();
        });
    }

    return app.exec();
}
