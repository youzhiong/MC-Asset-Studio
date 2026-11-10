#include "ui/MainWindow.h"

#include "core/AppPaths.h"
#include "core/AssetNamespace.h"
#include "core/AssociationTask.h"
#include "core/ExportService.h"
#include "core/GameEnvironment.h"
#include "core/GameSettings.h"
#include "core/ModCatalogTask.h"
#include "core/PackFormat.h"
#include "core/PathUtils.h"
#include "core/ProjectService.h"
#include "core/Version.h"
#include "core/ZipImporter.h"
#include "core/ZipArchive.h"
#include "ui/CollectionWindow.h"
#include "ui/DocumentArea.h"
#include "ui/ImageDocumentWidget.h"
#include "ui/InspectorPanel.h"
#include "ui/NewProjectDialog.h"
#include "ui/OpenProjectDialog.h"
#include "ui/PackEditorPage.h"
#include "ui/PreferencesDialog.h"
#include "ui/ProjectOverviewWidget.h"
#include "ui/ResourcePackImportDialog.h"
#include "ui/TextureBoardPage.h"
#include "ui/TextDocumentWidget.h"
#include "ui/TextureEditorPage.h"
#include "ui/TextureEditorWindow.h"
#include "ui/Theme.h"
#include "core/TextureCategory.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QScreen>
#include <QSaveFile>
#include <QSettings>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTextStream>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <utility>

namespace {

QFrame *makeDivider()
{
    auto *divider = new QFrame;
    divider->setObjectName(QStringLiteral("Divider"));
    divider->setFixedHeight(1);
    return divider;
}

QSettings appSettings()
{
    return QSettings(mcstudio::settingsFilePath(), QSettings::IniFormat);
}

QStringList recentProjectList()
{
    QSettings settings = appSettings();
    QStringList result;
    const QString stored = settings.value(QStringLiteral("project/recent")).toString();
    if (!stored.trimmed().isEmpty()) {
        const QJsonDocument document = QJsonDocument::fromJson(stored.toUtf8());
        if (document.isArray()) {
            for (const QJsonValue &value : document.array()) {
                const QString path = value.toString();
                if (!path.isEmpty())
                    result.append(path);
            }
        }
    }

    QStringList cleaned;
    for (const QString &path : std::as_const(result)) {
        const QString normalized = mcstudio::normalizedPath(path);
        if (normalized.isEmpty())
            continue;
        bool duplicate = false;
        for (const QString &existing : std::as_const(cleaned)) {
            if (mcstudio::samePath(existing, normalized)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            cleaned.append(normalized);
        if (cleaned.size() >= 10)
            break;
    }
    return cleaned;
}

void storeRecentProjectList(const QStringList &paths)
{
    QJsonArray array;
    for (const QString &path : paths)
        array.append(path);
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("project/recent"),
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

QString projectMenuName(const QString &projectDir)
{
    QFile file(QDir(projectDir).filePath(QStringLiteral("project.json")));
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (document.isObject()) {
            const QString name = document.object().value(QStringLiteral("name")).toString();
            if (!name.isEmpty())
                return name;
        }
    }
    return QFileInfo(projectDir).fileName();
}

QString projectOriginLabel(const QString &origin)
{
    if (origin == QLatin1String("created"))
        return QObject::tr("新建项目");
    if (origin == QLatin1String("folder"))
        return QObject::tr("文件夹导入");
    return QObject::tr("ZIP 导入");
}

bool writeProjectAsset(const mcstudio::Project &project, const QString &path,
                       const QByteArray &bytes, QString *error)
{
    QString relative = QDir::fromNativeSeparators(path);
    while (relative.startsWith(QLatin1Char('/')))
        relative.remove(0, 1);
    if (relative.isEmpty() || relative.contains(QLatin1String(".."))
        || QDir::isAbsolutePath(relative)) {
        if (error)
            *error = QObject::tr("非法资源路径：%1").arg(path);
        return false;
    }

    const QString target = project.absoluteOutputPath(relative);
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
        if (error)
            *error = QObject::tr("无法创建目录：%1").arg(QFileInfo(target).absolutePath());
        return false;
    }
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) < 0 || !file.commit()) {
        if (error)
            *error = QObject::tr("无法写入 %1：%2").arg(relative, file.errorString());
        return false;
    }
    return true;
}

void setBadgeText(QWidget *badge, const QString &text)
{
    if (!badge)
        return;
    if (auto *label = badge->findChild<QLabel *>())
        label->setText(text);
}

QWidget *makeBadge(const QString &text, bool accent = false)
{
    auto *frame = new QFrame;
    frame->setObjectName(accent ? QStringLiteral("VersionBadge")
                                : QStringLiteral("Badge"));
    frame->setAttribute(Qt::WA_StyledBackground, true);
    frame->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    auto *layout = new QHBoxLayout(frame);
    layout->setContentsMargins(accent ? 9 : 8, accent ? 3 : 2,
                               accent ? 9 : 8, accent ? 3 : 2);
    layout->setSpacing(0);
    auto *label = new QLabel(text);
    layout->addWidget(label);
    return frame;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("MC Asset Studio — 资源包工作台"));
    setWindowIcon(IconFactory::appIcon());
    setMinimumSize(1100, 720);

    m_gameSettings = mcstudio::loadGameSettings();

    createActions();
    createMenus();
    createToolBar();
    createCentralArea();
    createStatusBar();

    m_modPollTimer = new QTimer(this);
    m_modPollTimer->setInterval(120);
    QObject::connect(m_modPollTimer, &QTimer::timeout, this,
                     &MainWindow::pollModCatalog);

    m_associationPollTimer = new QTimer(this);
    m_associationPollTimer->setInterval(120);
    QObject::connect(m_associationPollTimer, &QTimer::timeout, this,
                     &MainWindow::pollAssociation);

    connectSignals();
    QObject::connect(qApp, &QApplication::focusChanged, this,
                     [this](QWidget *, QWidget *) { updateEditorActions(); });

    updateBreadcrumb();
    restoreWindowState();

    if (!QCoreApplication::arguments().contains(QStringLiteral("--import"))
        && !QCoreApplication::arguments().contains(QStringLiteral("--no-restore"))) {
        const QString lastDir =
            appSettings().value(QStringLiteral("project/lastDir")).toString();
        if (!lastDir.isEmpty() && QFileInfo::exists(lastDir)) {
            QString error;
            if (!openProject(lastDir, &error))
                statusBar()->showMessage(tr("最近项目无法打开：%1").arg(error), 5000);
        }
    }
}

MainWindow::~MainWindow()
{
    m_shuttingDown = true;
    // The collection window is parentless. Destroy it before this window's
    // members go away; its scene can emit selectionChanged during teardown and
    // would otherwise call back into a partially destroyed MainWindow.
    if (m_collectionWindow) {
        m_collectionWindow->onTextureSelected = {};
        m_collectionWindow->onOpenTexture = {};
        delete m_collectionWindow;
        m_collectionWindow = nullptr;
    }
    // Background association/catalog workers use Qt objects; wait for them so
    // closing the window cannot tear down Qt while a worker is still running.
    if (m_modTask)
        m_modTask->cancel();
    if (m_associationTask)
        m_associationTask->cancel();
    QThreadPool::globalInstance()->waitForDone();
    saveProjectSession();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmCloseTextureEditors(tr("退出"))) {
        event->ignore();
        return;
    }
    if (m_packEditorPage && m_packEditorPage->isDirty()) {
        const auto answer = QMessageBox::question(
            this, tr("有未保存的修改"),
            tr("材质包编辑页还有未保存的修改。\n退出会丢弃这些修改，继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    if (m_collectionWindow) {
        // The collection window is an independent top-level window now; close
        // it explicitly so quitting the main window never leaves an orphan.
        m_collectionWindow->close();
        m_collectionWindow = nullptr;
    }
    closeAllTextureWindows();
    if (m_editorFocusMode)
        applyEditorFocusMode(false);
    saveProjectSession();
    saveWindowState();
    QMainWindow::closeEvent(event);
}

void MainWindow::createActions()
{
    m_newAction = makeAction(tr("新建项目"), IconFactory::Glyph::Plus,
                             QKeySequence(QKeySequence::New));
    m_openAction = makeAction(tr("打开项目"), IconFactory::Glyph::Folder,
                              QKeySequence(QKeySequence::Open));
    m_importAction = makeAction(tr("导入 ZIP"), IconFactory::Glyph::Import,
                                QKeySequence(QStringLiteral("Ctrl+I")));
    m_importGameAction = new QAction(tr("从游戏目录导入…"), this);
    m_importGameAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+I")));
    m_importGameAction->setStatusTip(tr("列出实例 resourcepacks 目录中的资源包"));
    m_saveAction = makeAction(tr("保存"), IconFactory::Glyph::Save,
                              QKeySequence(QKeySequence::Save));
    m_exportAction = makeAction(tr("导出 ZIP"), IconFactory::Glyph::Export,
                                QKeySequence(QStringLiteral("Ctrl+E")));
    m_overwriteAction = new QAction(tr("覆盖原 ZIP…"), this);
    m_textureBoardAction = makeAction(tr("材质墙"), IconFactory::Glyph::Grid,
                                      QKeySequence(QStringLiteral("Ctrl+Shift+M")));
    m_collectionAction = makeAction(tr("集合模式窗口"), IconFactory::Glyph::Image,
                                    QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    m_openDataAction = new QAction(tr("打开数据目录"), this);
    m_exitAction = makeAction(tr("退出"), IconFactory::Glyph::Close,
                              QKeySequence(QKeySequence::Quit));

    m_undoAction = new QAction(tr("撤销"), this);
    m_undoAction->setShortcut(QKeySequence(QKeySequence::Undo));
    m_redoAction = new QAction(tr("重做"), this);
    m_redoAction->setShortcut(QKeySequence(QKeySequence::Redo));
    m_findAction = makeAction(tr("查找"), IconFactory::Glyph::Search,
                              QKeySequence(QKeySequence::Find));
    m_preferencesAction = new QAction(tr("首选项…"), this);
    m_preferencesAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    m_preferencesAction->setMenuRole(QAction::PreferencesRole);
    m_preferencesAction->setStatusTip(tr("游戏目录、多实例扫描、参考实例与缓存"));
    m_diagnosticsAction = new QAction(tr("关联诊断…"), this);
    m_diagnosticsAction->setStatusTip(tr("查看缺失模型/材质、未关联材质与未找到模组"));

    m_aboutAction = new QAction(tr("关于 MC Asset Studio"), this);
    m_aboutQtAction = new QAction(tr("关于 Qt"), this);

    m_toggleSidebarAction = new QAction(tr("资源库"), this);
    m_toggleSidebarAction->setCheckable(true);
    m_toggleSidebarAction->setChecked(true);
    m_toggleSidebarAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));

    m_toggleInspectorAction = new QAction(tr("检查器"), this);
    m_toggleInspectorAction->setCheckable(true);
    m_toggleInspectorAction->setChecked(true);
    m_toggleInspectorAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));

    m_resetLayoutAction = new QAction(tr("重置布局"), this);
    m_resetLayoutAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+0")));

    m_saveAction->setEnabled(false);
    m_exportAction->setEnabled(false);
    m_overwriteAction->setEnabled(false);
    m_textureBoardAction->setEnabled(false);
    m_collectionAction->setEnabled(false);
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
}

void MainWindow::createMenus()
{
    menuBar()->setNativeMenuBar(false);

    QMenu *fileMenu = menuBar()->addMenu(tr("文件(&F)"));
    fileMenu->addAction(m_newAction);
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_importAction);
    fileMenu->addAction(m_importGameAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_saveAction);
    fileMenu->addAction(m_exportAction);
    fileMenu->addAction(m_overwriteAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_openDataAction);
    fileMenu->addSeparator();

    m_recentMenu = fileMenu->addMenu(tr("最近项目"));
    refreshRecentMenu();

    fileMenu->addSeparator();
    fileMenu->addAction(m_exitAction);

    QMenu *editMenu = menuBar()->addMenu(tr("编辑(&E)"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_findAction);
    editMenu->addSeparator();
    editMenu->addAction(m_preferencesAction);
    editMenu->addAction(m_diagnosticsAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("视图(&V)"));
    viewMenu->addAction(m_textureBoardAction);
    viewMenu->addAction(m_collectionAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_toggleSidebarAction);
    viewMenu->addAction(m_toggleInspectorAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_resetLayoutAction);

    QMenu *helpMenu = menuBar()->addMenu(tr("帮助(&H)"));
    helpMenu->addAction(m_aboutAction);
    helpMenu->addAction(m_aboutQtAction);
}

void MainWindow::createToolBar()
{
    auto *toolbar = addToolBar(tr("主工具栏"));
    toolbar->setObjectName(QStringLiteral("MainToolBar"));
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setContextMenuPolicy(Qt::PreventContextMenu);

    toolbar->addAction(m_newAction);
    toolbar->addAction(m_openAction);
    toolbar->addAction(m_importAction);
    toolbar->addSeparator();
    toolbar->addAction(m_textureBoardAction);
    toolbar->addAction(m_saveAction);
    toolbar->addAction(m_exportAction);

    auto *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    auto *badge = makeBadge(tr("ZIP WORKSPACE"), true);
    badge->setToolTip(tr("ZIP 自动解包、标签页预览与导出已接入"));
    toolbar->addWidget(badge);

    auto *platform = new QLabel(QStringLiteral("Qt 6 · Win32"), toolbar);
    platform->setObjectName(QStringLiteral("Muted"));
    platform->setContentsMargins(8, 0, 2, 0);
    toolbar->addWidget(platform);
}

void MainWindow::createCentralArea()
{
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);

    m_sidebar = createSidebar();
    QWidget *center = createCenter();
    m_inspector = createInspector();

    m_splitter->addWidget(m_sidebar);
    m_splitter->addWidget(center);
    m_splitter->addWidget(m_inspector);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setSizes(QList<int>{260, 900, 320});

    m_sidebar->setMinimumWidth(230);
    center->setMinimumWidth(480);
    m_inspector->setMinimumWidth(270);

    auto *packRow = new QWidget;
    packRow->setObjectName(QStringLiteral("PackBarRow"));
    auto *packLayout = new QHBoxLayout(packRow);
    packLayout->setContentsMargins(10, 4, 10, 4);
    packLayout->setSpacing(8);

    auto *packLabel = new QLabel(tr("资源包"));
    packLabel->setObjectName(QStringLiteral("SectionTitle"));
    m_packBar = new QTabBar;
    m_packBar->setObjectName(QStringLiteral("PackBar"));
    m_packBar->setExpanding(false);
    m_packBar->setDrawBase(false);
    m_packBar->setTabsClosable(true);
    m_packBar->setMovable(true);
    m_packBar->setElideMode(Qt::ElideRight);
    m_packBar->setUsesScrollButtons(true);

    auto *addPackButton = new QToolButton;
    addPackButton->setAutoRaise(true);
    addPackButton->setIcon(IconFactory::icon(IconFactory::Glyph::Plus,
                                             Theme::colors().accent, 18));
    addPackButton->setToolTip(tr("导入另一个 ZIP（只激活一个包）"));

    packLayout->addWidget(packLabel);
    packLayout->addWidget(m_packBar);
    packLayout->addWidget(addPackButton);
    packLayout->addStretch();

    auto *central = new QWidget;
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(packRow);
    centralLayout->addWidget(m_splitter, 1);
    setCentralWidget(central);

    QObject::connect(addPackButton, &QToolButton::clicked, this,
                     [this] { importZipDialog(); });
    QObject::connect(m_packBar, &QTabBar::currentChanged, this,
                     &MainWindow::activatePack);
    QObject::connect(m_packBar, &QTabBar::tabCloseRequested, this,
                     &MainWindow::closePack);
}

QWidget *MainWindow::createSidebar()
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("Sidebar"));

    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto *headerRow = new QHBoxLayout;
    auto *title = new QLabel(tr("资源库"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    m_countBadge = makeBadge(QStringLiteral("0"));
    headerRow->addWidget(title);
    headerRow->addStretch();
    headerRow->addWidget(m_countBadge);
    layout->addLayout(headerRow);

    m_sidebarSearch = new QLineEdit;
    m_sidebarSearch->setPlaceholderText(tr("筛选资源…"));
    m_sidebarSearch->setClearButtonEnabled(true);
    m_sidebarSearch->addAction(
        IconFactory::icon(IconFactory::Glyph::Search, QColor(QStringLiteral("#64748b")), 16),
        QLineEdit::LeadingPosition);
    layout->addWidget(m_sidebarSearch);

    auto *modLabel = new QLabel(tr("模组"));
    modLabel->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(modLabel);

    m_modCombo = new QComboBox;
    m_modCombo->setObjectName(QStringLiteral("ModCombo"));
    m_modCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_modCombo->setMinimumContentsLength(18);
    m_modCombo->setToolTip(
        tr("只列出资源包中实际出现的 assets 命名空间；选择后材质墙与类型计数按该模组过滤。"));
    m_modCombo->addItem(tr("全部模组"), QString());
    m_modCombo->setEnabled(false);
    layout->addWidget(m_modCombo);

    auto *referenceTitle = new QLabel(tr("参考实例"));
    referenceTitle->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(referenceTitle);

    m_referenceCombo = new QComboBox;
    m_referenceCombo->setObjectName(QStringLiteral("ReferenceCombo"));
    m_referenceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_referenceCombo->setMinimumContentsLength(18);
    m_referenceCombo->setToolTip(
        tr("按 pack.mcmeta 的 pack_format 自动匹配，也可以强制指定实例；"
           "参考资源只用于补全结构，不会写入导出。"));
    m_referenceCombo->addItem(tr("自动匹配"), QString());
    m_referenceCombo->setEnabled(false);
    layout->addWidget(m_referenceCombo);

    m_referenceLabel = new QLabel(tr("参考实例：未配置游戏目录"));
    m_referenceLabel->setObjectName(QStringLiteral("PanelHint"));
    m_referenceLabel->setWordWrap(true);
    layout->addWidget(m_referenceLabel);

    auto *typeLabel = new QLabel(tr("资源类型"));
    typeLabel->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(typeLabel);

    m_categoryTree = new QTreeWidget;
    m_categoryTree->setColumnCount(2);
    m_categoryTree->setHeaderHidden(true);
    m_categoryTree->setIndentation(14);
    m_categoryTree->setRootIsDecorated(true);
    m_categoryTree->setAnimated(true);
    m_categoryTree->setUniformRowHeights(true);
    m_categoryTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_categoryTree->setFrameShape(QFrame::NoFrame);
    m_categoryTree->setIconSize(QSize(16, 16));
    m_categoryTree->setTextElideMode(Qt::ElideRight);
    m_categoryTree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_categoryTree->setMinimumHeight(270);
    m_categoryTree->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    m_categoryTree->setColumnWidth(1, 46);
    m_categoryTree->header()->setStretchLastSection(false);
    m_categoryTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_categoryTree->header()->setSectionResizeMode(1, QHeaderView::Fixed);

    m_categoryItems.clear();

    const auto addTop = [this](const QString &text, IconFactory::Glyph glyph,
                               const QString &key) -> QTreeWidgetItem * {
        auto *item = new QTreeWidgetItem(m_categoryTree);
        item->setText(0, text);
        item->setText(1, QStringLiteral("—"));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setIcon(0, IconFactory::icon(glyph, QColor(QStringLiteral("#94a3b8")), 16));
        item->setData(0, Qt::UserRole, key);
        m_categoryItems.insert(key, item);
        return item;
    };

    const auto addChild = [this](QTreeWidgetItem *parent, const QString &text,
                                 const QString &key) -> QTreeWidgetItem * {
        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, text);
        item->setText(1, QStringLiteral("—"));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, Qt::UserRole, key);
        m_categoryItems.insert(key, item);
        return item;
    };

    QTreeWidgetItem *allItem = addTop(tr("全部资源"), IconFactory::Glyph::Layers,
                                      QStringLiteral("all"));
    QTreeWidgetItem *textures = addTop(tr("材质"), IconFactory::Glyph::Image,
                                       QStringLiteral("texture"));
    addChild(textures, tr("全部"), QStringLiteral("texture.all"));
    for (const mcstudio::TextureCategoryInfo &category : mcstudio::textureCategories())
        addChild(textures, category.label, QStringLiteral("texture.") + category.key);

    addTop(tr("模型"), IconFactory::Glyph::Cube, QStringLiteral("model"));
    addTop(tr("动画"), IconFactory::Glyph::Play, QStringLiteral("animation"));
    addTop(tr("语言"), IconFactory::Glyph::Info, QStringLiteral("lang"));
    addTop(tr("声音"), IconFactory::Glyph::Info, QStringLiteral("sound"));
    addTop(tr("其他"), IconFactory::Glyph::Folder, QStringLiteral("other"));

    m_categoryTree->expandAll();
    m_categoryTree->setCurrentItem(allItem);
    layout->addWidget(m_categoryTree, 1);
    layout->addSpacing(12);

    layout->addWidget(makeDivider());

    auto *layerRow = new QHBoxLayout;
    auto *layerLabel = new QLabel(tr("图层"));
    layerLabel->setObjectName(QStringLiteral("SectionTitle"));
    auto *addLayerButton = new QToolButton;
    addLayerButton->setAutoRaise(true);
    addLayerButton->setIcon(IconFactory::icon(IconFactory::Glyph::Plus,
                                              QColor(QStringLiteral("#94a3b8")), 16));
    addLayerButton->setToolTip(tr("导入资源（下一阶段）"));
    layerRow->addWidget(layerLabel);
    layerRow->addStretch();
    layerRow->addWidget(addLayerButton);
    layout->addLayout(layerRow);

    m_layerHint = new QLabel(
        tr("还没有资源图层。\n新建项目、打开项目或导入 ZIP 后，"
           "source/ 和输出覆盖层 pack/ 会显示在这里。"));
    m_layerHint->setObjectName(QStringLiteral("PanelHint"));
    m_layerHint->setWordWrap(true);
    layout->addWidget(m_layerHint);

    m_layerImportButton = new QPushButton(tr("导入 ZIP"));
    m_layerImportButton->setProperty("role", QStringLiteral("ghost"));
    layout->addWidget(m_layerImportButton);

    layout->addStretch(1);
    layout->addWidget(makeDivider());

    auto *projectCard = new QFrame;
    projectCard->setObjectName(QStringLiteral("ProjectCard"));
    projectCard->setAttribute(Qt::WA_StyledBackground, true);
    auto *projectLayout = new QVBoxLayout(projectCard);
    projectLayout->setContentsMargins(10, 9, 10, 9);
    projectLayout->setSpacing(3);

    auto *statusRow = new QHBoxLayout;
    statusRow->setSpacing(6);
    auto *dot = new QLabel(QStringLiteral("●"));
    dot->setObjectName(QStringLiteral("ProjectDot"));
    m_projectStatusLabel = new QLabel(tr("未打开项目"));
    m_projectStatusLabel->setObjectName(QStringLiteral("Muted"));
    statusRow->addWidget(dot);
    statusRow->addWidget(m_projectStatusLabel);
    statusRow->addStretch();
    projectLayout->addLayout(statusRow);

    m_projectTargetLabel = new QLabel(tr("目标版本 · Java 1.20.1"));
    m_projectTargetLabel->setObjectName(QStringLiteral("PanelHint"));
    projectLayout->addWidget(m_projectTargetLabel);
    layout->addWidget(projectCard);

    QObject::connect(addLayerButton, &QToolButton::clicked, this,
                     [this] { importZipDialog(); });
    QObject::connect(m_layerImportButton, &QPushButton::clicked, this,
                     [this] { importZipDialog(); });

    return frame;
}

QWidget *MainWindow::createCenter()
{
    auto *frame = new QFrame;
    frame->setObjectName(QStringLiteral("CenterPanel"));

    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget;
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 12, 18, 12);
    headerLayout->setSpacing(10);

    m_breadcrumb = new QLabel(tr("资源浏览"));
    m_breadcrumb->setObjectName(QStringLiteral("PanelTitle"));
    headerLayout->addWidget(m_breadcrumb);
    headerLayout->addStretch();

    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(tr("搜索资源路径，例如 stone.png"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(320);
    m_searchEdit->addAction(
        IconFactory::icon(IconFactory::Glyph::Search, QColor(QStringLiteral("#64748b")), 16),
        QLineEdit::LeadingPosition);
    headerLayout->addWidget(m_searchEdit);

    auto *gridButton = new QToolButton;
    gridButton->setCheckable(true);
    gridButton->setChecked(true);
    gridButton->setAutoRaise(true);
    gridButton->setIconSize(QSize(18, 18));
    gridButton->setIcon(IconFactory::icon(IconFactory::Glyph::Grid,
                                          QColor(QStringLiteral("#94a3b8")), 18));
    gridButton->setToolTip(tr("网格视图"));

    auto *listButton = new QToolButton;
    listButton->setCheckable(true);
    listButton->setAutoRaise(true);
    listButton->setIconSize(QSize(18, 18));
    listButton->setIcon(IconFactory::icon(IconFactory::Glyph::List,
                                          QColor(QStringLiteral("#94a3b8")), 18));
    listButton->setToolTip(tr("列表视图将在下一阶段提供"));
    listButton->setEnabled(false);

    auto *viewGroup = new QButtonGroup(frame);
    viewGroup->setExclusive(true);
    viewGroup->addButton(gridButton);
    viewGroup->addButton(listButton);

    headerLayout->addWidget(gridButton);
    headerLayout->addWidget(listButton);
    layout->addWidget(header);
    layout->addWidget(makeDivider());

    m_documentArea = new mcstudio::DocumentArea;
    m_documentArea->beforeClose = [this](const QString &key) {
        if (key.startsWith(QLatin1String("edit:"))) {
            auto *editor = m_textureEditors.value(key).data();
            if (!editor || !editor->isDirty())
                return true;
            const auto answer = QMessageBox::warning(
                this, tr("未保存的材质"),
                tr("“%1”还有未保存的修改。").arg(editor->relativePath()),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
                QMessageBox::Save);
            if (answer == QMessageBox::Save)
                return editor->saveDocument();
            return answer == QMessageBox::Discard;
        }
        if (key == QLatin1String("__packeditor__") && m_packEditorPage
            && m_packEditorPage->isDirty()) {
            const auto answer = QMessageBox::question(
                this, tr("有未保存的修改"),
                tr("材质包编辑页还有未保存的修改。\n关闭会丢弃这些修改，继续？"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            return answer == QMessageBox::Yes;
        }
        return true;
    };
    QObject::connect(m_documentArea, &QTabWidget::currentChanged, this,
                     [this](int) { updateEditorActions(); });

    auto *browserTab = new QWidget;
    auto *browserLayout = new QVBoxLayout(browserTab);
    browserLayout->setContentsMargins(0, 0, 0, 0);
    browserLayout->setSpacing(0);

    auto *empty = new QWidget;
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->setContentsMargins(48, 32, 48, 40);
    emptyLayout->setSpacing(12);
    emptyLayout->addStretch(1);

    auto *icon = new QLabel;
    icon->setPixmap(IconFactory::appIcon().pixmap(QSize(96, 96)));
    icon->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(icon);

    auto *title = new QLabel(tr("新建或打开一个项目"));
    title->setObjectName(QStringLiteral("EmptyTitle"));
    title->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(title);

    auto *text = new QLabel(
        tr("新建空白资源包、打开已有项目，或导入 ZIP / 资源包文件夹。\n"
           "项目会复制到 %1，原 ZIP 和原文件夹保持不变。")
            .arg(QDir::toNativeSeparators(mcstudio::AppPaths::projectsRoot())));
    text->setObjectName(QStringLiteral("EmptyText"));
    text->setWordWrap(true);
    text->setAlignment(Qt::AlignCenter);
    text->setFixedWidth(560);
    text->setMinimumHeight(56);
    text->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
    emptyLayout->addWidget(text, 0, Qt::AlignHCenter);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(10);
    buttonRow->addStretch();
    auto *newProjectButton = new QPushButton(tr("新建项目"));
    newProjectButton->setObjectName(QStringLiteral("PrimaryButton"));
    newProjectButton->setMinimumWidth(120);
    auto *openProjectButton = new QPushButton(tr("打开项目"));
    openProjectButton->setObjectName(QStringLiteral("GhostButton"));
    openProjectButton->setMinimumWidth(120);
    auto *importZipButton = new QPushButton(tr("导入 ZIP"));
    importZipButton->setObjectName(QStringLiteral("GhostButton"));
    importZipButton->setMinimumWidth(120);
    auto *dataFolderButton = new QPushButton(tr("打开数据目录"));
    dataFolderButton->setObjectName(QStringLiteral("GhostButton"));
    dataFolderButton->setMinimumWidth(120);
    buttonRow->addWidget(newProjectButton);
    buttonRow->addWidget(openProjectButton);
    buttonRow->addWidget(importZipButton);
    buttonRow->addWidget(dataFolderButton);
    buttonRow->addStretch();
    emptyLayout->addSpacing(6);
    emptyLayout->addLayout(buttonRow);

    auto *chipRow = new QHBoxLayout;
    chipRow->setSpacing(8);
    chipRow->addStretch();
    for (const QString &chipText :
         {tr("新建空包"), tr("文件夹导入"), tr("ZIP 自动解包")}) {
        auto *chip = makeBadge(chipText);
        chipRow->addWidget(chip);
    }
    chipRow->addStretch();
    emptyLayout->addSpacing(14);
    emptyLayout->addLayout(chipRow);

    auto *hint = new QLabel(tr("Windows 10/11 · Java Edition 1.20.1 · 设置和缓存位于 AppData"));
    hint->setObjectName(QStringLiteral("PanelHint"));
    hint->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(hint);
    emptyLayout->addStretch(2);

    browserLayout->addWidget(empty, 1);
    m_documentArea->setBrowserPage(browserTab);
    layout->addWidget(m_documentArea, 1);

    QObject::connect(newProjectButton, &QPushButton::clicked, this,
                     [this] { newProjectDialog(); });
    QObject::connect(openProjectButton, &QPushButton::clicked, this,
                     [this] { openProjectDialog(); });
    QObject::connect(importZipButton, &QPushButton::clicked, this,
                     [this] { importZipDialog(); });
    QObject::connect(dataFolderButton, &QPushButton::clicked, this,
                     [this] { openDataFolder(); });

    return frame;
}

QWidget *MainWindow::createInspector()
{
    m_inspectorPanel = new mcstudio::InspectorPanel;
    m_inspectorPanel->onImportReference = [this](const QString &path) {
        importReferenceTexture(path);
    };
    return m_inspectorPanel;
}

void MainWindow::createStatusBar()
{
    auto *bar = statusBar();
    bar->setSizeGripEnabled(true);

    m_statusLabel = new QLabel(tr("就绪 · 新建/打开项目或导入 ZIP"));
    m_statusLabel->setObjectName(QStringLiteral("StatusText"));
    m_selectionLabel = new QLabel(tr("0 个文件"));
    m_selectionLabel->setObjectName(QStringLiteral("Muted"));

    m_taskProgress = new QProgressBar;
    m_taskProgress->setFixedWidth(170);
    m_taskProgress->setFixedHeight(12);
    m_taskProgress->setTextVisible(false);
    m_taskProgress->setRange(0, 1);
    m_taskProgress->setValue(0);
    m_taskProgress->setVisible(false);
    m_taskProgress->setToolTip(tr("后台任务进度"));

    auto *versionLabel = new QLabel(tr("v%1").arg(mcstudio::version()));
    versionLabel->setObjectName(QStringLiteral("Muted"));
    versionLabel->setToolTip(mcstudio::buildSummary());

    bar->addWidget(m_statusLabel, 1);
    bar->addPermanentWidget(m_taskProgress);
    bar->addPermanentWidget(m_selectionLabel);
    bar->addPermanentWidget(versionLabel);
}

void MainWindow::connectSignals()
{
    QObject::connect(m_newAction, &QAction::triggered, this,
                     &MainWindow::newProjectDialog);
    QObject::connect(m_openAction, &QAction::triggered, this,
                     &MainWindow::openProjectDialog);
    QObject::connect(m_importAction, &QAction::triggered, this,
                     [this] { importZipDialog(); });
    QObject::connect(m_importGameAction, &QAction::triggered, this,
                     &MainWindow::importFromGameDirectory);
    QObject::connect(m_saveAction, &QAction::triggered, this,
                     &MainWindow::saveProject);
    QObject::connect(m_undoAction, &QAction::triggered, this, [this] {
        if (auto *editor = activeTextureEditor())
            editor->undo();
    });
    QObject::connect(m_redoAction, &QAction::triggered, this, [this] {
        if (auto *editor = activeTextureEditor())
            editor->redo();
    });
    QObject::connect(m_exportAction, &QAction::triggered, this,
                     [this] { exportProjectDialog(false); });
    QObject::connect(m_overwriteAction, &QAction::triggered, this,
                     [this] { exportProjectDialog(true); });
    QObject::connect(m_textureBoardAction, &QAction::triggered, this,
                     &MainWindow::openTextureBoard);
    QObject::connect(m_collectionAction, &QAction::triggered, this,
                     &MainWindow::openCollectionWindow);
    QObject::connect(m_openDataAction, &QAction::triggered, this,
                     [this] { openDataFolder(); });
    QObject::connect(m_exitAction, &QAction::triggered, this, &QWidget::close);

    QObject::connect(m_findAction, &QAction::triggered, this, [this] {
        m_searchEdit->setFocus(Qt::ShortcutFocusReason);
        m_searchEdit->selectAll();
        statusBar()->showMessage(tr("搜索将在资源索引接入后可用。"), 3000);
    });

    QObject::connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    QObject::connect(m_preferencesAction, &QAction::triggered, this,
                     &MainWindow::openPreferencesDialog);
    QObject::connect(m_diagnosticsAction, &QAction::triggered, this,
                     &MainWindow::showAssociationDiagnostics);
    QObject::connect(m_aboutQtAction, &QAction::triggered, this,
                     [] { QApplication::aboutQt(); });

    QObject::connect(m_categoryTree, &QTreeWidget::currentItemChanged, this,
                     [this](QTreeWidgetItem *, QTreeWidgetItem *) {
                         updateBreadcrumb();
                         applyCategoryFilter();
                     });

    QObject::connect(m_modCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_modCombo)
            return;
        const QString selected = m_modCombo->currentData().toString();
        if (selected == m_namespaceFilter)
            return;
        m_namespaceFilter = selected;
        applyModFilter();
    });

    QObject::connect(m_referenceCombo, &QComboBox::currentIndexChanged, this,
                     [this](int) { onReferenceComboChanged(); });

    QObject::connect(m_sidebarSearch, &QLineEdit::textChanged, m_searchEdit,
                     &QLineEdit::setText);

    QObject::connect(m_searchEdit, &QLineEdit::returnPressed, this, [this] {
        statusBar()->showMessage(tr("已按路径筛选材质墙。"), 3500);
    });

    QObject::connect(m_searchEdit, &QLineEdit::textChanged, this,
                     [this](const QString &text) {
                         if (m_textureBoardPage)
                             m_textureBoardPage->setSearchFilter(text);
                     });

    QObject::connect(m_toggleSidebarAction, &QAction::toggled, m_sidebar,
                     &QWidget::setVisible);
    QObject::connect(m_toggleInspectorAction, &QAction::toggled, m_inspector,
                     &QWidget::setVisible);
    QObject::connect(m_resetLayoutAction, &QAction::triggered, this,
                     &MainWindow::resetLayout);
}

void MainWindow::importZipDialog()
{
    QSettings settings = appSettings();
    const QString lastZip = settings.value(QStringLiteral("paths/lastZip")).toString();

    const QString zipPath = QFileDialog::getOpenFileName(
        this, tr("导入资源包 ZIP"), lastZip,
        tr("Minecraft 资源包 (*.zip *.jar);;ZIP (*.zip);;所有文件 (*.*)"));
    if (zipPath.isEmpty())
        return;

    settings.setValue(QStringLiteral("paths/lastZip"), zipPath);

    QString error;
    if (!importZip(zipPath, &error)) {
        QMessageBox::critical(this, tr("导入失败"), error);
        return;
    }
}

void MainWindow::importFromGameDirectory()
{
    mcstudio::ResourcePackImportDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString zipPath = dialog.selectedPath();
    if (zipPath.isEmpty())
        return;

    QString error;
    if (!importZip(zipPath, &error))
        QMessageBox::critical(this, tr("导入失败"), error);
}

void MainWindow::newProjectDialog()
{
    mcstudio::NewProjectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const mcstudio::ProjectCreateResult result = mcstudio::ProjectService::createBlank(
        mcstudio::AppPaths::projectsRoot(), dialog.request());
    QApplication::restoreOverrideCursor();

    if (!result.ok) {
        QMessageBox::critical(this, tr("新建项目失败"), result.error);
        return;
    }

    QString error;
    if (!openProject(result.projectDir, &error))
        QMessageBox::critical(this, tr("打开新项目失败"), error);
}

void MainWindow::openProjectDialog()
{
    mcstudio::OpenProjectDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (!dialog.selectedProjectDir().isEmpty()) {
        openProjectPath(dialog.selectedProjectDir());
        return;
    }
    if (!dialog.browsedFolder().isEmpty())
        openProjectPath(dialog.browsedFolder());
}

void MainWindow::openProjectPath(const QString &path)
{
    const QString normalized = mcstudio::normalizedPath(path);
    if (normalized.isEmpty())
        return;

    const QFileInfo info(normalized);
    if (info.isFile()) {
        const QString suffix = info.suffix().toLower();
        if (suffix == QLatin1String("zip") || suffix == QLatin1String("jar")) {
            QString error;
            if (!importZip(normalized, &error))
                QMessageBox::critical(this, tr("导入失败"), error);
        } else {
            QMessageBox::warning(this, tr("无法打开"),
                                 tr("不支持的文件类型：%1").arg(info.fileName()));
        }
        return;
    }

    if (!info.isDir()) {
        QMessageBox::warning(this, tr("无法打开"), tr("路径不存在：%1").arg(path));
        return;
    }

    if (QFileInfo::exists(QDir(normalized).filePath(QStringLiteral("project.json")))) {
        QString error;
        if (!openProject(normalized, &error) && !error.isEmpty())
            QMessageBox::critical(this, tr("打开项目失败"), error);
        return;
    }

    if (QFileInfo::exists(QDir(normalized).filePath(QStringLiteral("pack.mcmeta")))) {
        const auto answer = QMessageBox::question(
            this, tr("打开资源包文件夹"),
            tr("这个文件夹不是 MC Asset Studio 项目，但它是一个资源包。\n"
               "是否复制一份并作为新项目打开？原文件夹不会被修改。"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes)
            return;

        QProgressDialog progress(tr("正在复制资源包文件夹…"), QString(), 0, 100, this);
        progress.setWindowTitle(tr("导入文件夹"));
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);
        progress.setAutoClose(false);

        const mcstudio::ProjectCreateResult result =
            mcstudio::ProjectService::importFolder(
                normalized, mcstudio::AppPaths::projectsRoot(), info.fileName(),
                [&progress](int done, int total, const QString &message) {
                    progress.setMaximum(qMax(1, total));
                    progress.setValue(done);
                    progress.setLabelText(message);
                    QCoreApplication::processEvents();
                });
        progress.close();

        if (!result.ok) {
            QMessageBox::critical(this, tr("导入文件夹失败"), result.error);
            return;
        }
        QString error;
        if (!openProject(result.projectDir, &error))
            QMessageBox::critical(this, tr("打开项目失败"), error);
        return;
    }

    QMessageBox::warning(
        this, tr("无法打开"),
        tr("所选文件夹既没有 project.json，也没有 pack.mcmeta。\n"
           "请选择 MC Asset Studio 项目目录或资源包根目录。"));
}

void MainWindow::saveProject()
{
    if (auto *editor = activeTextureEditor()) {
        editor->saveDocument();
        return;
    }
    if (!m_hasProject)
        return;
    m_project.updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QString error;
    if (!m_project.save(&error)) {
        QMessageBox::critical(this, tr("保存失败"), error);
        return;
    }
    statusBar()->showMessage(tr("项目信息已保存。"), 3000);
}

QStringList MainWindow::recentProjects() const
{
    return recentProjectList();
}

void MainWindow::rememberRecentProject(const QString &projectDir)
{
    const QString normalized = mcstudio::normalizedPath(projectDir);
    if (normalized.isEmpty())
        return;

    QStringList list;
    list.append(normalized);
    for (const QString &existing : recentProjectList()) {
        if (!mcstudio::samePath(existing, normalized))
            list.append(existing);
        if (list.size() >= 10)
            break;
    }
    storeRecentProjectList(list);
    refreshRecentMenu();
}

void MainWindow::refreshRecentMenu()
{
    if (!m_recentMenu)
        return;

    m_recentMenu->clear();
    const QStringList recent = recentProjectList();
    if (recent.isEmpty()) {
        QAction *empty = m_recentMenu->addAction(tr("（暂无）"));
        empty->setEnabled(false);
        return;
    }

    for (const QString &dir : recent) {
        const bool exists = QFileInfo(dir).isDir();
        QString label = projectMenuName(dir);
        if (!exists)
            label += tr("（目录缺失）");
        QAction *action = m_recentMenu->addAction(label);
        action->setToolTip(QDir::toNativeSeparators(dir));
        action->setEnabled(exists);
        connect(action, &QAction::triggered, this, [this, dir] {
            openProjectPath(dir);
        });
    }

    m_recentMenu->addSeparator();
    QAction *clear = m_recentMenu->addAction(tr("清除最近记录"));
    connect(clear, &QAction::triggered, this, [this] {
        storeRecentProjectList({});
        refreshRecentMenu();
    });
}

bool MainWindow::importZip(const QString &zipPath, QString *error)
{
    if (zipPath.isEmpty()) {
        if (error)
            *error = tr("未选择 ZIP 文件");
        return false;
    }

    QProgressDialog progress(tr("准备导入 ZIP…"), QString(), 0, 100, this);
    progress.setWindowTitle(tr("导入资源包"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setCancelButton(nullptr);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.show();
    QCoreApplication::processEvents();

    const mcstudio::ImportResult result = mcstudio::ZipImporter::import(
        zipPath, mcstudio::AppPaths::projectsRoot(), mcstudio::AppPaths::cacheRoot(),
        [&progress](int done, int total, const QString &message) {
            const int maximum = qMax(1, total);
            progress.setMaximum(maximum);
            progress.setValue(qBound(0, done, maximum));
            progress.setLabelText(message);
            QCoreApplication::processEvents();
        });
    progress.close();

    if (!result.ok) {
        if (error)
            *error = result.error;
        statusBar()->showMessage(tr("导入失败：%1").arg(result.error), 6000);
        return false;
    }

    if (!openProject(result.projectDir, error))
        return false;

    statusBar()->showMessage(
        result.fromCache
            ? tr("已复用缓存项目 · %1 个文件").arg(result.fileCount)
            : tr("ZIP 已自动解包 · %1 个文件").arg(result.fileCount),
        6000);
    return true;
}

bool MainWindow::openProjectDirectory(const QString &projectDir, QString *error)
{
    return openProject(projectDir, error);
}

bool MainWindow::openProject(const QString &projectDir, QString *error)
{
    mcstudio::Project project;
    QString loadError;
    if (!project.load(projectDir, &loadError)) {
        if (error)
            *error = loadError;
        return false;
    }

    if (!confirmCloseTextureEditors(tr("切换项目"))) {
        if (error)
            *error = tr("已取消切换项目。");
        return false;
    }

    if (m_hasProject)
        saveProjectSession();
    if (m_packEditorPage && m_packEditorPage->isDirty()) {
        const auto answer = QMessageBox::question(
            this, tr("有未保存的修改"),
            tr("材质包编辑页还有未保存的修改。\n切换项目会丢弃这些修改，继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            if (error)
                *error = tr("已取消切换项目。");
            return false;
        }
    }
    if (m_collectionWindow) {
        m_collectionWindow->close();
        m_collectionWindow = nullptr;
    }
    if (m_modTask) {
        m_modTask->cancel();
    }
    if (m_modPollTimer)
        m_modPollTimer->stop();
    m_modCatalog.reset();
    m_namespaceFilter.clear();
    m_referenceMatch = mcstudio::ReferenceMatch{};
    m_assetStack.reset();
    m_association.reset();
    m_stackDiagnostics.clear();
    m_selectedTexturePath.clear();

    m_project = project;
    m_hasProject = true;
    m_activePackId = m_project.id;
    addPackTab(m_project);
    if (m_inspectorPanel)
        m_inspectorPanel->clear();

    auto beforeClose = m_documentArea->beforeClose;
    m_documentArea->beforeClose = nullptr;
    m_documentArea->removeDocument(QStringLiteral("__overview__"));
    m_documentArea->removeDocument(QStringLiteral("__textures__"));
    m_documentArea->closeAllDocuments();
    m_documentArea->beforeClose = beforeClose;
    m_documentArea->setBrowserVisible(false);
    closeAllTextureWindows();
    m_textureEditors.clear();
    m_exportAction->setEnabled(true);
    m_overwriteAction->setEnabled(!m_project.sourceZip.isEmpty());
    m_textureBoardAction->setEnabled(true);
    m_collectionAction->setEnabled(true);
    setWindowTitle(tr("%1 — MC Asset Studio").arg(m_project.name));
    updateProjectUi();
    openOverviewTab();
    openTextureBoard();
    rebuildCategoryIndex();
    rebuildModCombo();
    updateCategoryCounts();
    startModCatalogForProject();

    if (QTreeWidgetItem *allTextures = m_categoryItems.value(QStringLiteral("texture.all")))
        m_categoryTree->setCurrentItem(allTextures);
    applyCategoryFilter();

    restoreProjectSession();

    // The wall is the primary browsing surface; keep it active after tab
    // restoration so importing a pack lands on the material display.
    if (m_documentArea->hasDocument(QStringLiteral("__textures__")))
        m_documentArea->activateDocument(QStringLiteral("__textures__"));

    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("project/lastDir"), m_project.rootDir);
    rememberRecentProject(m_project.rootDir);
    updateEditorActions();
    return true;
}

void MainWindow::updateProjectUi()
{
    if (!m_hasProject)
        return;

    setBadgeText(m_countBadge, QString::number(m_project.fileCount));
    m_projectStatusLabel->setText(tr("已打开"));
    m_projectTargetLabel->setText(
        tr("pack_format %1 · %2 个文件")
            .arg(m_project.packFormat)
            .arg(m_project.fileCount));
    const QString originPath = !m_project.sourceZip.isEmpty()
                                   ? m_project.sourceZip
                                   : m_project.originPath;
    m_projectTargetLabel->setToolTip(originPath);
    m_statusLabel->setText(
        tr("已加载 %1 · %2 个文件").arg(m_project.name).arg(m_project.fileCount));
    m_selectionLabel->setText(tr("%1 个文件").arg(m_project.fileCount));

    if (m_layerHint) {
        QString sourceLine;
        if (!m_project.sourceZip.isEmpty()) {
            sourceLine = tr("源 ZIP：%1").arg(QFileInfo(m_project.sourceZip).fileName());
        } else if (!m_project.originPath.isEmpty()) {
            sourceLine = tr("来源文件夹：%1").arg(QDir::toNativeSeparators(m_project.originPath));
        } else {
            sourceLine = tr("来源：新建空资源包");
        }
        m_layerHint->setText(
            tr("source/ 工作副本\npack/ 为输出覆盖层\n%1").arg(sourceLine));
    }
    if (m_layerImportButton)
        m_layerImportButton->setText(tr("导入另一个 ZIP"));
}

void MainWindow::openOverviewTab()
{
    if (!m_hasProject)
        return;

    auto *overview = new mcstudio::ProjectOverviewWidget(m_project);
    overview->onExportNew = [this] { exportProjectDialog(false); };
    overview->onOverwrite = [this] { exportProjectDialog(true); };
    overview->onOpenFolder = [this] { openProjectFolder(); };
    overview->onOpenPackEditor = [this] { openPackEditorTab(); };
    overview->onPackMetaSaved = [this](int format, const QVector<int> &supported,
                                       const QString &description) {
        m_project.packFormat = format;
        m_project.supportedFormats = supported;
        m_project.description = description;
        m_project.updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        QString error;
        if (!m_project.save(&error)) {
            QMessageBox::critical(this, tr("保存失败"), error);
            return;
        }
        if (m_documentArea->hasDocument(QStringLiteral("pack.mcmeta")))
            m_documentArea->removeDocument(QStringLiteral("pack.mcmeta"));
        updateProjectUi();
        startModCatalogForProject();
        statusBar()->showMessage(tr("pack.mcmeta 已保存，正在重新匹配参考实例…"), 4000);
    };

    m_documentArea->addFixedDocument(
        QStringLiteral("__overview__"), tr("项目概览"),
        IconFactory::icon(IconFactory::Glyph::Info,
                          QColor(QStringLiteral("#94a3b8")), 16),
        overview);
}

void MainWindow::addPackTab(const mcstudio::Project &project)
{
    if (!m_packBar || project.id.isEmpty())
        return;
    m_packDirs.insert(project.id, project.rootDir);

    for (int index = 0; index < m_packBar->count(); ++index) {
        if (m_packBar->tabData(index).toString() == project.id) {
            QSignalBlocker blocker(m_packBar);
            m_packBar->setCurrentIndex(index);
            return;
        }
    }

    QSignalBlocker blocker(m_packBar);
    const int index = m_packBar->addTab(
        QFileInfo(project.sourceZip).fileName().isEmpty()
            ? project.name
            : QFileInfo(project.sourceZip).fileName());
    m_packBar->setTabData(index, project.id);
    m_packBar->setTabToolTip(index, project.sourceZip);
    m_packBar->setCurrentIndex(index);
}

void MainWindow::activatePack(int index)
{
    if (m_switchingPack || index < 0 || index >= m_packBar->count())
        return;

    const QString id = m_packBar->tabData(index).toString();
    if (id.isEmpty() || id == m_activePackId)
        return;

    const QString projectDir = m_packDirs.value(id);
    if (projectDir.isEmpty())
        return;

    m_switchingPack = true;
    QString error;
    if (!openProject(projectDir, &error)) {
        QSignalBlocker blocker(m_packBar);
        for (int i = 0; i < m_packBar->count(); ++i) {
            if (m_packBar->tabData(i).toString() == m_activePackId) {
                m_packBar->setCurrentIndex(i);
                break;
            }
        }
        statusBar()->showMessage(tr("无法切换资源包：%1").arg(error), 6000);
    }
    m_switchingPack = false;
}

void MainWindow::closePack(int index)
{
    if (index < 0 || index >= m_packBar->count())
        return;

    const QString id = m_packBar->tabData(index).toString();
    const bool wasActive = id == m_activePackId;
    if (wasActive && !confirmCloseTextureEditors(tr("关闭这个项目")))
        return;
    if (wasActive && m_packEditorPage && m_packEditorPage->isDirty()) {
        const auto answer = QMessageBox::question(
            this, tr("有未保存的修改"),
            tr("材质包编辑页还有未保存的修改。\n关闭这个项目会丢弃这些修改，继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    m_packDirs.remove(id);

    {
        QSignalBlocker blocker(m_packBar);
        m_packBar->removeTab(index);
    }

    if (m_packBar->count() == 0) {
        clearProject();
        return;
    }

    if (wasActive) {
        const int next = qBound(0, index, m_packBar->count() - 1);
        m_packBar->setCurrentIndex(next);
        activatePack(next);
    }
}

void MainWindow::clearProject()
{
    saveProjectSession();
    if (m_collectionWindow) {        m_collectionWindow->close();
        m_collectionWindow = nullptr;
    }
    if (m_modTask)
        m_modTask->cancel();
    if (m_modPollTimer)
        m_modPollTimer->stop();
    m_modCatalog.reset();
    m_projectNamespaces.clear();
    m_namespaceFilter.clear();
    m_environment = mcstudio::GameEnvironmentSnapshot{};
    m_referenceMatch = mcstudio::ReferenceMatch{};
    m_assetStack.reset();
    m_association.reset();
    m_stackDiagnostics.clear();
    m_selectedTexturePath.clear();
    m_categoryCountsAll.clear();
    m_categoryCountsByNamespace.clear();
    m_packEditorPage = nullptr;
    closeAllTextureWindows();
    m_textureEditors.clear();

    m_hasProject = false;
    m_project = mcstudio::Project{};
    m_activePackId.clear();
    if (m_inspectorPanel)
        m_inspectorPanel->clear();
    m_packDirs.clear();

    if (m_documentArea) {
        auto beforeClose = m_documentArea->beforeClose;
        m_documentArea->beforeClose = nullptr;
        m_documentArea->removeDocument(QStringLiteral("__overview__"));
        m_documentArea->removeDocument(QStringLiteral("__textures__"));
        m_documentArea->closeAllDocuments();
        m_documentArea->beforeClose = beforeClose;
        m_documentArea->setBrowserVisible(true);
        m_documentArea->activateDocument(QStringLiteral("__browser__"));
    }
    m_exportAction->setEnabled(false);
    m_overwriteAction->setEnabled(false);
    m_textureBoardAction->setEnabled(false);
    m_collectionAction->setEnabled(false);
    setWindowTitle(tr("MC Asset Studio — 资源包工作台"));

    setBadgeText(m_countBadge, QStringLiteral("0"));
    if (m_projectStatusLabel)
        m_projectStatusLabel->setText(tr("未打开项目"));
    if (m_projectTargetLabel)
        m_projectTargetLabel->setText(tr("目标版本 · Java 1.20.1"));
    if (m_statusLabel)
        m_statusLabel->setText(tr("就绪 · 新建/打开项目或导入 ZIP"));
    if (m_selectionLabel)
        m_selectionLabel->setText(tr("0 个文件"));
    if (m_layerHint)
        m_layerHint->setText(tr("还没有资源图层。\n新建项目、打开项目或导入 ZIP 后，"
                                "source/ 和输出覆盖层 pack/ 会显示在这里。"));
    if (m_layerImportButton)
        m_layerImportButton->setText(tr("导入 ZIP"));
    if (m_modCombo) {
        QSignalBlocker blocker(m_modCombo);
        m_modCombo->clear();
        m_modCombo->addItem(tr("全部模组"), QString());
        m_modCombo->setEnabled(false);
    }
    if (m_referenceCombo) {
        m_updatingReferenceCombo = true;
        QSignalBlocker blocker(m_referenceCombo);
        m_referenceCombo->clear();
        m_referenceCombo->addItem(tr("自动匹配"), QString());
        m_referenceCombo->setEnabled(false);
        m_updatingReferenceCombo = false;
    }
    if (m_referenceLabel) {
        m_referenceLabel->setText(tr("参考实例：未配置游戏目录"));
        m_referenceLabel->setStyleSheet(QString());
        m_referenceLabel->setToolTip(QString());
    }
    for (QTreeWidgetItem *item : std::as_const(m_categoryItems)) {
        item->setText(1, QStringLiteral("—"));
        item->setHidden(false);
    }
    if (m_categoryItems.contains(QStringLiteral("all")))
        m_categoryTree->setCurrentItem(m_categoryItems.value(QStringLiteral("all")));
    updateEditorActions();
}

bool MainWindow::applyPackEditorCover(const QString &imagePath)
{
    if (!m_hasProject)
        return false;
    const QImage image(imagePath);
    if (image.isNull())
        return false;
    openPackEditorTab();
    if (!m_packEditorPage)
        return false;
    m_packEditorPage->setCoverForTesting(image);
    return true;
}

void MainWindow::showPackEditorAllForTesting()
{
    openPackEditorTab();
    if (m_packEditorPage)
        m_packEditorPage->setScopeAllForTesting(true);
}

bool MainWindow::applyPackMetaForTesting(int packFormat, const QString &description)
{
    if (!m_hasProject || packFormat <= 0)
        return false;
    mcstudio::ProjectOverviewWidget widget(m_project);
    widget.onPackMetaSaved = [this](int format, const QVector<int> &supported,
                                    const QString &desc) {
        m_project.packFormat = format;
        m_project.supportedFormats = supported;
        m_project.description = desc;
        m_project.updatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        QString error;
        if (!m_project.save(&error)) {
            QMessageBox::critical(this, tr("保存失败"), error);
            return;
        }
        updateProjectUi();
        startModCatalogForProject();
    };
    return widget.applyPackMetaValues(packFormat, description,
                                      m_project.supportedFormats);
}

void MainWindow::openPackEditor()
{
    openPackEditorTab();
}

bool MainWindow::applyPackEditorNamespace(const QString &namespaceId, bool supported)
{
    if (!m_hasProject || namespaceId.isEmpty())
        return false;
    openPackEditorTab();
    if (!m_packEditorPage)
        return false;
    m_packEditorPage->setNamespaceSupported(namespaceId, supported);
    m_packEditorPage->saveNow();
    return true;
}

void MainWindow::openPackEditorTab()
{
    if (!m_hasProject)
        return;
    if (m_documentArea->hasDocument(QStringLiteral("__packeditor__"))) {
        m_documentArea->activateDocument(QStringLiteral("__packeditor__"));
        return;
    }

    auto *page = new mcstudio::PackEditorPage(m_project, m_environment,
                                              m_referenceMatch.instanceKey);
    m_packEditorPage = page;
    page->onSaved = [this] {
        mcstudio::Project reloaded;
        QString error;
        if (reloaded.load(m_project.rootDir, &error)) {
            m_project = reloaded;
            updateProjectUi();
        }
        if (m_documentArea->hasDocument(QStringLiteral("pack.png")))
            m_documentArea->removeDocument(QStringLiteral("pack.png"));
        const bool editorActive =
            m_documentArea->hasDocument(QStringLiteral("__packeditor__"));
        m_documentArea->removeDocument(QStringLiteral("__overview__"));
        openOverviewTab();
        if (editorActive)
            m_documentArea->activateDocument(QStringLiteral("__packeditor__"));
        statusBar()->showMessage(tr("材质包修改已保存。"), 4000);
        startModCatalogForProject();
    };
    page->onOpenOverview = [this] {
        m_documentArea->activateDocument(QStringLiteral("__overview__"));
    };

    m_documentArea->openDocument(
        QStringLiteral("__packeditor__"), tr("材质包编辑"),
        IconFactory::icon(IconFactory::Glyph::Save,
                          QColor(QStringLiteral("#fbbf24")), 16),
        page);
}

void MainWindow::openTextureBoard()
{
    if (!m_hasProject)
        return;

    if (m_documentArea->hasDocument(QStringLiteral("__textures__"))) {
        m_documentArea->activateDocument(QStringLiteral("__textures__"));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool showReferenceTextures =
        mcstudio::loadGameSettings().showReferenceTextures;
    auto *page = new mcstudio::TextureBoardPage(m_project, m_assetStack.get(),
                                                showReferenceTextures);
    QApplication::restoreOverrideCursor();

    m_textureBoardPage = page;
    page->onOpenTexture = [this](const QString &path) { openTextureEditor(path); };
    page->onViewTexture = [this](const QString &path) { openImageTab(path); };
    page->onTextureSelected = [this](const QString &path) {
        showTextureInInspector(path);
    };
    page->onImportReference = [this](const QString &path) {
        importReferenceTexture(path);
    };
    page->onImportReferenceNamespace = [this](const QString &namespaceId) {
        importReferenceNamespace(namespaceId);
    };
    page->onOpenCollection = [this] { openCollectionWindow(); };
    if (m_searchEdit)
        page->setSearchFilter(m_searchEdit->text());
    m_documentArea->addFixedDocument(
        QStringLiteral("__textures__"), tr("材质墙"),
        IconFactory::icon(IconFactory::Glyph::Grid,
                          Theme::colors().accent, 16),
        page);
}

void MainWindow::showTextureInInspector(const QString &path)
{
    if (!m_inspectorPanel || !m_hasProject)
        return;

    m_selectedTexturePath = path;
    if (path.isEmpty()) {
        m_inspectorPanel->clear();
        return;
    }

    if (m_project.exists(path)) {
        m_inspectorPanel->setTexture(m_project, path);
        m_inspectorPanel->setReferenceInfo(referenceInfoForTexture(path));
        return;
    }

    if (!m_assetStack || !m_assetStack->exists(path)) {
        m_inspectorPanel->clear();
        return;
    }

    const QByteArray bytes = m_assetStack->read(path);
    if (bytes.isEmpty()) {
        m_inspectorPanel->clear();
        return;
    }
    const mcstudio::AssetLocation location = m_assetStack->locate(path);
    m_inspectorPanel->setTextureBytes(
        path, bytes, m_assetStack->read(path + QLatin1String(".mcmeta")),
        tr("参考 · %1").arg(location.label), true);
    m_inspectorPanel->setReferenceInfo(
        tr("参考来源：%1（未导入）").arg(location.label));
    m_inspectorPanel->setAssociationHint(
        tr("这是参考实例里的材质，资源包尚未覆盖。\n"
           "右键卡片或点击“导入到资源包”可复制到 pack/ 覆盖层。"));
}

bool MainWindow::importReferenceTexture(const QString &path)
{
    if (!m_hasProject || !m_assetStack || path.isEmpty())
        return false;
    if (m_project.exists(path)) {
        statusBar()->showMessage(tr("这个材质已经在资源包内：%1").arg(path), 5000);
        return false;
    }
    if (!m_assetStack->exists(path)) {
        statusBar()->showMessage(tr("参考实例中找不到材质：%1").arg(path), 5000);
        return false;
    }

    const mcstudio::AssetLocation location = m_assetStack->locate(path);
    if (!location.fromReference()) {
        statusBar()->showMessage(tr("该路径不是参考材质：%1").arg(path), 5000);
        return false;
    }

    const QByteArray bytes = m_assetStack->read(path);
    QString error;
    if (bytes.isEmpty() || !writeProjectAsset(m_project, path, bytes, &error)) {
        QMessageBox::critical(this, tr("导入参考材质失败"),
                              error.isEmpty() ? tr("无法读取参考材质。") : error);
        return false;
    }
    const QByteArray mcmeta = m_assetStack->read(path + QLatin1String(".mcmeta"));
    if (!mcmeta.isEmpty()) {
        QString metaError;
        if (!writeProjectAsset(m_project, path + QLatin1String(".mcmeta"), mcmeta,
                               &metaError)) {
            QMessageBox::critical(this, tr("导入参考材质失败"), metaError);
            return false;
        }
    }

    m_project.scan();
    refreshTextureBoardAfterReferenceImport();
    statusBar()->showMessage(tr("已导入参考材质：%1").arg(path), 5000);
    return true;
}

bool MainWindow::importReferenceNamespace(const QString &namespaceId)
{
    if (!m_hasProject || !m_assetStack)
        return false;

    const QString prefix = namespaceId.isEmpty()
                               ? QStringLiteral("assets/")
                               : QStringLiteral("assets/%1/").arg(namespaceId);
    QStringList candidates;
    for (const QString &path : m_assetStack->list(prefix)) {
        if (!path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive)
            || !path.contains(QLatin1String("/textures/"))) {
            continue;
        }
        if (m_project.fileIndex().contains(path))
            continue;
        const mcstudio::AssetLocation location = m_assetStack->locate(path);
        if (location.fromReference())
            candidates.append(path);
    }

    if (candidates.isEmpty()) {
        QMessageBox::information(this, tr("导入参考材质"),
                                 tr("这个范围内没有可导入的参考材质。"));
        return false;
    }

    const QString scope = namespaceId.isEmpty() ? tr("全部参考材质")
                                                : tr("命名空间 %1").arg(namespaceId);
    const auto answer = QMessageBox::question(
        this, tr("导入参考材质"),
        tr("将 %1 个参考材质从 %2 复制到 pack/ 覆盖层。\n"
           "已存在的包内材质不会覆盖。继续？")
            .arg(candidates.size())
            .arg(scope),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes)
        return false;

    QProgressDialog progress(tr("正在导入参考材质…"), QString(), 0,
                             candidates.size(), this);
    progress.setWindowTitle(tr("导入参考材质"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(300);
    progress.setAutoClose(false);
    progress.setCancelButton(nullptr);

    int imported = 0;
    QStringList failures;
    for (int index = 0; index < candidates.size(); ++index) {
        const QString &path = candidates.at(index);
        const QByteArray bytes = m_assetStack->read(path);
        QString error;
        if (bytes.isEmpty() || !writeProjectAsset(m_project, path, bytes, &error)) {
            failures.append(path);
            continue;
        }
        const QByteArray mcmeta = m_assetStack->read(path + QLatin1String(".mcmeta"));
        if (!mcmeta.isEmpty())
            writeProjectAsset(m_project, path + QLatin1String(".mcmeta"), mcmeta, nullptr);
        ++imported;
        progress.setValue(index + 1);
        progress.setLabelText(tr("已导入 %1/%2").arg(imported).arg(candidates.size()));
        if (index % 20 == 0)
            QCoreApplication::processEvents();
    }
    progress.close();

    m_project.scan();
    refreshTextureBoardAfterReferenceImport();
    statusBar()->showMessage(
        tr("参考材质导入完成：%1 个，失败 %2 个").arg(imported).arg(failures.size()),
        6000);
    return imported > 0;
}

void MainWindow::refreshTextureBoardAfterReferenceImport()
{
    m_project.scan();
    m_projectNamespaces = mcstudio::namespacesFromPaths(m_project.files());
    rebuildModCombo();
    updateProjectUi();
    updateCategoryCounts();
    if (m_textureBoardPage)
        m_textureBoardPage->refreshPackEntries(m_project);
    if (m_collectionWindow && m_textureBoardPage) {
        m_collectionWindow->setEntries(m_textureBoardPage->entriesWithReferences());
        m_collectionWindow->setReferenceContext(m_assetStack, m_modCatalog,
                                                m_projectNamespaces);
    }
    // The pack changed, so the block/texture association and its cached names
    // are stale. Rebuild them now instead of leaving the collection window on
    // an empty 0-group list. If the mod catalog is still scanning, that task
    // will rebuild the stack and start the association itself; starting a
    // second association against a stack without mod jars would race it.
    if (!(m_modTask && m_modTask->isRunning()))
        QTimer::singleShot(0, this, [this] { startAssociationBuild(); });
    if (!m_selectedTexturePath.isEmpty())
        showTextureInInspector(m_selectedTexturePath);
}

void MainWindow::fitCollectionForTesting()
{
    if (m_collectionWindow)
        m_collectionWindow->fitAll();
}

void MainWindow::setShowReferenceTextures(bool show)
{
    if (!m_hasProject)
        return;
    if (!m_textureBoardPage)
        openTextureBoard();
    if (m_textureBoardPage)
        m_textureBoardPage->setShowReferenceTextures(show);
}

bool MainWindow::importReferencePath(const QString &path)
{
    return importReferenceTexture(path);
}

bool MainWindow::importReferenceNamespaceForTesting(const QString &namespaceId)
{
    return importReferenceNamespace(namespaceId);
}

void MainWindow::openCollectionWindow()
{
    if (!m_hasProject)
        return;

    if (!m_textureBoardPage)
        openTextureBoard();
    if (!m_textureBoardPage)
        return;

    if (m_collectionWindow) {
        if (m_collectionWindow->isMinimized())
            m_collectionWindow->showNormal();
        m_collectionWindow->raise();
        m_collectionWindow->activateWindow();
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    // No parent: an owned window is excluded from the Windows taskbar. The
    // collection window must behave like a normal top-level application window.
    auto *window = new mcstudio::CollectionWindow(
        m_project, m_textureBoardPage->entriesWithReferences(),
        m_textureBoardPage->categoryFilter(), nullptr);
    window->setWindowFlag(Qt::Window, true);
    window->onTextureSelected = [this](const QString &path) {
        showTextureInInspector(path);
    };
    window->onOpenTexture = [this](const QString &path) {
        if (!openTextureEditor(path))
            return;
        // The collection window is an independent top-level window, so the
        // editor tab in the main window may otherwise stay behind it.
        if (isMinimized())
            showNormal();
        raise();
        activateWindow();
    };
    window->setReferenceContext(m_assetStack, m_modCatalog, m_projectNamespaces);
    if (m_association)
        window->setAssociationResult(m_association);
    window->setNamespaceFilter(m_namespaceFilter,
                               m_namespaceFilter.isEmpty() ? QString()
                                                           : currentModLabel());
    QApplication::restoreOverrideCursor();
    window->show();
    window->raise();
    window->activateWindow();
    m_collectionWindow = window;
}

void MainWindow::selectCategory(const QString &key)
{
    if (QTreeWidgetItem *item = m_categoryItems.value(key))
        m_categoryTree->setCurrentItem(item);
}

void MainWindow::selectMod(const QString &namespaceId)
{
    if (!m_modCombo)
        return;
    const int index = m_modCombo->findData(namespaceId);
    if (index >= 0)
        m_modCombo->setCurrentIndex(index);
}

void MainWindow::selectReferenceInstance(const QString &instanceKey)
{
    if (!m_referenceCombo)
        return;

    int index = m_referenceCombo->findData(instanceKey);
    if (index < 0) {
        for (int i = 0; i < m_referenceCombo->count(); ++i) {
            const QString key = m_referenceCombo->itemData(i).toString();
            const mcstudio::InstanceInfo *instance = m_environment.findInstance(key);
            if (instance
                && (instance->key == instanceKey
                    || instance->name.contains(instanceKey, Qt::CaseInsensitive))) {
                index = i;
                break;
            }
        }
    }
    if (index >= 0)
        m_referenceCombo->setCurrentIndex(index);
}

void MainWindow::selectTexture(const QString &relativePath)
{
    showTextureInInspector(relativePath);
}

void MainWindow::setSearchText(const QString &text)
{
    if (m_searchEdit)
        m_searchEdit->setText(text);
}

void MainWindow::selectPackTab(int index)
{
    if (m_packBar && index >= 0 && index < m_packBar->count())
        m_packBar->setCurrentIndex(index);
}

void MainWindow::showCollectionWindow()
{
    openCollectionWindow();
}

void MainWindow::setCollectionAssociationMode()
{
    setCollectionGroupMode(QStringLiteral("association"));
}

void MainWindow::setCollectionGroupMode(const QString &mode)
{
    if (m_collectionWindow)
        m_collectionWindow->setGroupMode(mode);
}

void MainWindow::applyCategoryFilter()
{
    if (!m_hasProject || !m_categoryTree)
        return;

    QTreeWidgetItem *item = m_categoryTree->currentItem();
    if (!item)
        return;
    QString key = item->data(0, Qt::UserRole).toString();
    if (key == QLatin1String("texture"))
        key = QStringLiteral("texture.all");
    if (key != QLatin1String("all") && key != QLatin1String("animation")
        && !key.startsWith(QLatin1String("texture."))) {
        return;
    }

    if (!m_textureBoardPage)
        openTextureBoard();
    if (m_textureBoardPage)
        m_textureBoardPage->setCategoryFilter(key);
}

void MainWindow::addCategoryCount(const QString &namespaceId, const QString &key)
{
    ++m_categoryCountsAll[key];
    if (!namespaceId.isEmpty())
        ++m_categoryCountsByNamespace[namespaceId][key];
}

void MainWindow::rebuildCategoryIndex()
{
    m_categoryCountsAll.clear();
    m_categoryCountsByNamespace.clear();
    if (!m_hasProject)
        return;

    for (const QString &path : m_project.files()) {
        const QString ns = mcstudio::namespaceFromAssetPath(path);
        addCategoryCount(ns, QStringLiteral("all"));
        if (path.contains(QLatin1String("/blockstates/"))
            || path.contains(QLatin1String("/models/"))) {
            addCategoryCount(ns, QStringLiteral("model"));
        }
        if (path.contains(QLatin1String("/lang/")))
            addCategoryCount(ns, QStringLiteral("lang"));
        if (path.contains(QLatin1String("/sounds")) || path.endsWith(QLatin1String(".ogg")))
            addCategoryCount(ns, QStringLiteral("sound"));
    }

    if (m_textureBoardPage) {
        for (const mcstudio::TextureBoardEntry &entry : m_textureBoardPage->entries()) {
            const QString category = entry.categoryKey.isEmpty()
                                         ? mcstudio::textureCategoryForPath(entry.path)
                                         : entry.categoryKey;
            addCategoryCount(entry.namespaceId, QStringLiteral("texture.all"));
            addCategoryCount(entry.namespaceId, QStringLiteral("texture.") + category);
            if (entry.animated)
                addCategoryCount(entry.namespaceId, QStringLiteral("animation"));
        }
    }
}

void MainWindow::updateCategoryCounts()
{
    if (!m_categoryTree)
        return;

    for (QTreeWidgetItem *item : std::as_const(m_categoryItems))
        item->setText(1, QStringLiteral("—"));
    if (!m_hasProject)
        return;

    const QHash<QString, int> counts =
        m_namespaceFilter.isEmpty()
            ? m_categoryCountsAll
            : m_categoryCountsByNamespace.value(m_namespaceFilter);
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (QTreeWidgetItem *item = m_categoryItems.value(it.key())) {
            item->setText(1, it.value() > 0 ? QString::number(it.value())
                                            : QStringLiteral("—"));
        }
    }

    // The mod dropdown only shows what the pack actually contains; mirror
    // that in the type tree by hiding empty texture categories for a mod.
    for (auto it = m_categoryItems.constBegin(); it != m_categoryItems.constEnd(); ++it) {
        if (!it.key().startsWith(QLatin1String("texture."))
            || it.key() == QLatin1String("texture.all")) {
            continue;
        }
        it.value()->setHidden(!m_namespaceFilter.isEmpty()
                              && counts.value(it.key(), 0) == 0);
    }

    if (QTreeWidgetItem *texturesItem = m_categoryItems.value(QStringLiteral("texture"))) {
        const int textureCount = counts.value(QStringLiteral("texture.all"), 0);
        texturesItem->setText(1, textureCount > 0 ? QString::number(textureCount)
                                                  : QStringLiteral("—"));
    }
}

void MainWindow::rebuildModCombo()
{
    if (!m_modCombo)
        return;

    const QString keep = m_namespaceFilter;
    {
        QSignalBlocker blocker(m_modCombo);
        m_modCombo->clear();
        m_modCombo->addItem(tr("全部模组"), QString());

        for (const QString &ns : std::as_const(m_projectNamespaces)) {
            QString label = ns;
            bool found = true;
            QString detail;
            if (m_modCatalog) {
                if (const mcstudio::NamespaceMod *mapping = m_modCatalog->entry(ns)) {
                    label = mapping->label();
                    found = mapping->found;
                    detail = mapping->sourceLabel();
                    if (!mapping->version.isEmpty())
                        detail += QStringLiteral(" · ") + mapping->version;
                    if (!mapping->jarPath.isEmpty())
                        detail += QLatin1Char('\n') + mapping->jarPath;
                    if (mapping->ambiguous)
                        detail += QLatin1Char('\n') + tr("存在多个候选 jar");
                }
            }

            const int textureCount = m_categoryCountsByNamespace.value(ns)
                                         .value(QStringLiteral("texture.all"));
            QString text;
            if (!m_modCatalog)
                text = ns;
            else if (!found)
                text = tr("%1 · 未找到模组").arg(ns);
            else {
                text = label;
                if (label != ns)
                    text += QStringLiteral(" · ") + ns;
            }
            text += tr("（%1）").arg(textureCount);

            m_modCombo->addItem(text, ns);
            m_modCombo->setItemData(m_modCombo->count() - 1, detail, Qt::ToolTipRole);
        }

        int index = m_modCombo->findData(keep);
        if (index < 0) {
            index = 0;
            m_namespaceFilter.clear();
        }
        m_modCombo->setCurrentIndex(index);
    }

    m_modCombo->setEnabled(m_hasProject && !m_projectNamespaces.isEmpty());
}

QString MainWindow::currentModLabel() const
{
    if (m_namespaceFilter.isEmpty())
        return tr("全部模组");
    if (m_modCatalog) {
        if (const mcstudio::NamespaceMod *mapping = m_modCatalog->entry(m_namespaceFilter))
            return mapping->label();
    }
    return m_namespaceFilter;
}

void MainWindow::applyModFilter()
{
    if (!m_hasProject)
        return;

    const QString label = m_namespaceFilter.isEmpty() ? QString() : currentModLabel();
    if (m_textureBoardPage)
        m_textureBoardPage->setNamespaceFilter(m_namespaceFilter, label);
    updateCategoryCounts();

    if (m_categoryTree) {
        QTreeWidgetItem *item = m_categoryTree->currentItem();
        const QString key = item ? item->data(0, Qt::UserRole).toString() : QString();
        if (!key.isEmpty() && key != QLatin1String("all")) {
            const QHash<QString, int> counts =
                m_namespaceFilter.isEmpty()
                    ? m_categoryCountsAll
                    : m_categoryCountsByNamespace.value(m_namespaceFilter);
            const bool fallbackable = key.startsWith(QLatin1String("texture."))
                                      || key == QLatin1String("animation");
            if (fallbackable && counts.value(key, 0) <= 0) {
                if (QTreeWidgetItem *allTextures =
                        m_categoryItems.value(QStringLiteral("texture.all"))) {
                    m_categoryTree->setCurrentItem(allTextures);
                }
            }
        }
    }

    updateBreadcrumb();
    if (m_collectionWindow)
        m_collectionWindow->setNamespaceFilter(m_namespaceFilter, label);
}

const mcstudio::InstanceInfo *MainWindow::referenceInstanceInfo() const
{
    if (!m_referenceMatch.found)
        return nullptr;
    return m_environment.findInstance(m_referenceMatch.instanceKey);
}

void MainWindow::rebuildReferenceCombo()
{
    if (!m_referenceCombo)
        return;

    m_updatingReferenceCombo = true;
    QSignalBlocker blocker(m_referenceCombo);
    m_referenceCombo->clear();
    m_referenceCombo->addItem(
        tr("自动匹配（pack_format %1）").arg(m_project.packFormat), QString());

    const bool multipleRoots = m_environment.roots.size() > 1;
    for (const mcstudio::GameRootInfo &root : m_environment.roots) {
        for (const mcstudio::InstanceInfo &instance : root.instances) {
            const QString label =
                multipleRoots
                    ? tr("%1 · %2 · %3 · %4")
                          .arg(instance.name, instance.versionLabel(),
                               instance.loaderLabel(), root.name)
                    : tr("%1 · %2 · %3").arg(instance.name, instance.versionLabel(),
                                             instance.loaderLabel());
            m_referenceCombo->addItem(label, instance.key);
            m_referenceCombo->setItemData(
                m_referenceCombo->count() - 1,
                QStringLiteral("%1\n%2").arg(instance.instancePath, instance.clientJar),
                Qt::ToolTipRole);
        }
    }

    int index = m_referenceCombo->findData(m_project.referenceInstanceKey);
    if (index < 0)
        index = 0;
    m_referenceCombo->setCurrentIndex(index);
    m_referenceCombo->setEnabled(m_hasProject && m_environment.instanceCount() > 0);
    m_updatingReferenceCombo = false;
}

void MainWindow::onReferenceComboChanged()
{
    if (m_updatingReferenceCombo || !m_hasProject || !m_referenceCombo)
        return;

    const QString key = m_referenceCombo->currentData().toString();
    if (key == m_project.referenceInstanceKey)
        return;

    m_project.referenceInstanceKey = key;
    QString error;
    if (!m_project.saveReferenceConfig(&error) && !error.isEmpty())
        statusBar()->showMessage(tr("无法保存项目参考实例：%1").arg(error), 5000);

    m_association.reset();
    startModCatalogForProject();
}

void MainWindow::updateReferenceLabel()
{
    if (!m_referenceLabel)
        return;

    if (!m_referenceMatch.found) {
        m_referenceLabel->setStyleSheet(
            QStringLiteral("color: %1;").arg(Theme::colors().warning.name()));
        m_referenceLabel->setText(
            !m_referenceMatch.warning.isEmpty()
                ? m_referenceMatch.warning
                : (m_gameSettings.roots.isEmpty()
                       ? tr("参考实例：未配置（编辑 → 首选项 → 游戏目录）")
                       : tr("参考实例：未匹配，请手动选择")));
        m_referenceLabel->setToolTip(m_stackDiagnostics.join(QLatin1Char('\n')));
        return;
    }

    QString text = tr("参考：%1\n%2 · %3")
                       .arg(m_referenceMatch.instanceName, m_referenceMatch.clientVersion,
                            mcstudio::loaderDisplayName(m_referenceMatch.loader));
    if (!m_referenceMatch.compatible) {
        text += tr("\n! 与 pack_format %1 不完全匹配").arg(m_project.packFormat);
    }
    m_referenceLabel->setText(text);

    QStringList details;
    if (m_assetStack)
        details.append(tr("来源层：%1").arg(m_assetStack->providerCount()));
    details.append(m_referenceMatch.reason);
    if (!m_referenceMatch.warning.isEmpty())
        details.append(m_referenceMatch.warning);
    details.append(m_stackDiagnostics);
    m_referenceLabel->setToolTip(details.join(QLatin1Char('\n')));
    m_referenceLabel->setStyleSheet(
        m_referenceMatch.compatible
            ? QString()
            : QStringLiteral("color: %1;").arg(Theme::colors().warning.name()));
}

void MainWindow::rebuildAssetStack()
{
    if (m_associationTask)
        m_associationTask->cancel();
    if (m_associationPollTimer)
        m_associationPollTimer->stop();
    m_association.reset();

    if (!m_hasProject) {
        m_assetStack.reset();
        m_stackDiagnostics.clear();
        return;
    }

    mcstudio::ReferenceStackOptions options;
    options.fallbackModels = m_gameSettings.fallbackModels;
    options.fallbackLang = m_gameSettings.fallbackLang;

    const mcstudio::ModCatalog emptyCatalog;
    const mcstudio::ModCatalog &catalog = m_modCatalog ? *m_modCatalog : emptyCatalog;
    const mcstudio::ReferenceStackResult result = mcstudio::buildReferenceAssetStack(
        m_project, referenceInstanceInfo(), catalog, m_projectNamespaces, options);

    m_assetStack = result.stack;
    m_stackDiagnostics = result.diagnostics;
    m_association.reset();

    if (m_textureBoardPage)
        m_textureBoardPage->setReferenceStack(*m_assetStack);
    if (m_collectionWindow && m_textureBoardPage)
        m_collectionWindow->setEntries(m_textureBoardPage->entriesWithReferences());

    if (m_collectionWindow)
        m_collectionWindow->setReferenceContext(m_assetStack, m_modCatalog,
                                                m_projectNamespaces);
    updateReferenceLabel();
}

void MainWindow::startModCatalogForProject()
{
    if (m_modTask)
        m_modTask->cancel();
    if (m_modPollTimer)
        m_modPollTimer->stop();
    if (m_associationTask)
        m_associationTask->cancel();
    if (m_associationPollTimer)
        m_associationPollTimer->stop();
    m_modCatalog.reset();
    m_association.reset();

    m_projectNamespaces = mcstudio::namespacesFromPaths(m_project.files());
    for (const QString &ns : m_project.supportedNamespaces) {
        if (!ns.isEmpty() && !m_projectNamespaces.contains(ns))
            m_projectNamespaces.append(ns);
    }
    m_gameSettings = mcstudio::loadGameSettings();

    m_environment = mcstudio::GameEnvironment::loadCachedSnapshot(m_gameSettings.roots);
    if (m_environment.instanceCount() == 0 && !m_gameSettings.roots.isEmpty())
        m_environment = mcstudio::GameEnvironment::scanRoots(m_gameSettings.roots, {});

    m_referenceMatch = mcstudio::matchReferenceInstance(
        m_environment, m_gameSettings, m_project.packFormat, m_project.supportedFormats,
        m_project.referenceInstanceKey);
    rebuildReferenceCombo();

    const mcstudio::InstanceInfo *reference = referenceInstanceInfo();
    // Baseline stack: pack/source + (if a reference exists) client jar and
    // assets objects. Mod jars are added once the catalog is ready.
    rebuildAssetStack();
    rebuildModCombo();

    if (!reference) {
        updateReferenceLabel();
        startAssociationBuild();
        statusBar()->showMessage(
            m_referenceMatch.warning.isEmpty()
                ? tr("未选择参考实例，仅使用资源包内容。")
                : m_referenceMatch.warning,
            8000);
        return;
    }

    QVector<mcstudio::InstanceInfo> others;
    for (const mcstudio::GameRootInfo &root : m_environment.roots) {
        for (const mcstudio::InstanceInfo &instance : root.instances) {
            if (instance.key != reference->key)
                others.append(instance);
        }
    }

    if (!m_modTask)
        m_modTask = std::make_unique<mcstudio::ModCatalogTask>();

    statusBar()->showMessage(tr("正在解析模组元数据（%1）…").arg(reference->name), 0);
    setTaskProgress(true, 0, 0, tr("正在解析模组元数据（%1）…").arg(reference->name));

    m_modTask->start(
        *reference, others, m_projectNamespaces, true,
        [this](int done, int total, const QString &message) {
            setTaskProgress(true, done, total,
                            tr("解析模组元数据 %1/%2 · %3")
                                .arg(done)
                                .arg(total)
                                .arg(message));
        },
        [this](const mcstudio::ModCatalog &catalog, bool ok, const QString &error) {
            if (m_modPollTimer)
                m_modPollTimer->stop();
            setTaskProgress(false);
            if (!ok) {
                statusBar()->showMessage(tr("模组元数据解析失败：%1").arg(error), 6000);
                startAssociationBuild();
                return;
            }
            applyModCatalog(catalog);
        });
    m_modPollTimer->start();
}

void MainWindow::pollModCatalog()
{
    if (!m_modTask) {
        if (m_modPollTimer)
            m_modPollTimer->stop();
        return;
    }
    m_modTask->poll();
    if (!m_modTask->isRunning() && m_modPollTimer)
        m_modPollTimer->stop();
}

void MainWindow::applyModCatalog(const mcstudio::ModCatalog &catalog)
{
    m_modCatalog = std::make_shared<mcstudio::ModCatalog>(catalog);
    if (m_textureBoardPage)
        m_textureBoardPage->setModCatalog(m_modCatalog);

    rebuildModCombo();
    const QString label = m_namespaceFilter.isEmpty() ? QString() : currentModLabel();
    if (m_textureBoardPage)
        m_textureBoardPage->setNamespaceFilter(m_namespaceFilter, label);
    if (m_collectionWindow && m_textureBoardPage) {
        m_collectionWindow->setEntries(m_textureBoardPage->entries());
        m_collectionWindow->setNamespaceFilter(m_namespaceFilter, label);
    }

    rebuildAssetStack();
    updateBreadcrumb();

    statusBar()->showMessage(
        tr("模组元数据：%1 个命名空间 · %2 个已匹配 · 参考实例 %3 · 来源层 %4")
            .arg(m_modCatalog->entries().size())
            .arg(m_modCatalog->foundCount())
            .arg(m_referenceMatch.instanceName)
            .arg(m_assetStack ? m_assetStack->providerCount() : 0),
        6000);
    startAssociationBuild();
}

void MainWindow::startAssociationBuild()
{
    if (m_associationTask)
        m_associationTask->cancel();
    if (m_associationPollTimer)
        m_associationPollTimer->stop();

    setTaskProgress(false);
    m_association.reset();
    if (!m_hasProject || !m_assetStack)
        return;

    m_associationCacheKey.zipSha1 = m_project.zipSha1;
    m_associationCacheKey.contentFingerprint =
        m_project.zipSha1.isEmpty() ? m_project.contentFingerprint : QString();
    m_associationCacheKey.referenceInstanceKey = m_referenceMatch.instanceKey;
    m_associationCacheKey.fallbackModels = m_gameSettings.fallbackModels;
    m_associationCacheKey.fallbackLang = m_gameSettings.fallbackLang;

    mcstudio::AssociationResult cached;
    if (mcstudio::loadAssociationCache(m_project, m_associationCacheKey, &cached)) {
        m_association = std::make_shared<mcstudio::AssociationResult>(std::move(cached));
        if (m_collectionWindow)
            m_collectionWindow->setAssociationResult(m_association);
        if (!m_selectedTexturePath.isEmpty() && m_inspectorPanel) {
            m_inspectorPanel->setReferenceInfo(
                referenceInfoForTexture(m_selectedTexturePath));
        }
        statusBar()->showMessage(
            tr("已加载模型关联缓存：%1 组").arg(m_association->groups.size()), 4000);
        return;
    }

    if (!m_associationTask)
        m_associationTask = std::make_unique<mcstudio::AssociationTask>();

    setTaskProgress(true, 0, 0, tr("正在解析模型关联…"));
    m_associationTask->start(
        m_project, m_assetStack, m_modCatalog, m_projectNamespaces,
        [this](int done, int total, const QString &message) {
            setTaskProgress(true, done, total, message);
        },
        [this](const mcstudio::AssociationResult &result, bool ok,
               const QString &error) {
            if (m_associationPollTimer)
                m_associationPollTimer->stop();
            setTaskProgress(false);
            if (!ok) {
                statusBar()->showMessage(tr("模型关联解析失败：%1").arg(error), 6000);
                return;
            }

            m_association = std::make_shared<mcstudio::AssociationResult>(result);
            mcstudio::saveAssociationCache(m_project, m_associationCacheKey, result);
            if (m_collectionWindow)
                m_collectionWindow->setAssociationResult(m_association);
            if (!m_selectedTexturePath.isEmpty() && m_inspectorPanel) {
                m_inspectorPanel->setReferenceInfo(
                    referenceInfoForTexture(m_selectedTexturePath));
            }
            statusBar()->showMessage(
                tr("模型关联：%1 组（方块 %2 / 物品 %3）· 参考补全 %4 组")
                    .arg(m_association->groups.size())
                    .arg(m_association->blockGroups)
                    .arg(m_association->itemGroups)
                    .arg(m_association->referenceGroups),
                6000);
        });
    m_associationPollTimer->start();
}

void MainWindow::pollAssociation()
{
    if (!m_associationTask) {
        if (m_associationPollTimer)
            m_associationPollTimer->stop();
        return;
    }
    m_associationTask->poll();
    if (!m_associationTask->isRunning() && m_associationPollTimer)
        m_associationPollTimer->stop();
}

void MainWindow::setTaskProgress(bool busy, int done, int total, const QString &message)
{
    if (!m_taskProgress)
        return;

    m_taskProgress->setVisible(busy);
    if (!busy) {
        statusBar()->clearMessage();
        return;
    }

    if (total > 0) {
        m_taskProgress->setRange(0, total);
        m_taskProgress->setValue(done);
    } else {
        m_taskProgress->setRange(0, 0);
    }
    if (!message.isEmpty())
        statusBar()->showMessage(message);
}

bool MainWindow::waitForCatalogReady(int timeoutMs)
{
    if (!m_hasProject)
        return false;
    if (!m_referenceMatch.found)
        return true;

    QElapsedTimer timer;
    timer.start();
    while (!m_modCatalog && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    return m_modCatalog != nullptr;
}

bool MainWindow::waitForAssociationReady(int timeoutMs)
{
    if (!m_hasProject)
        return false;

    QElapsedTimer timer;
    timer.start();
    while (!m_association && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
        QThread::msleep(10);
    }
    return m_association != nullptr;
}

const mcstudio::AssociationResult &MainWindow::associationResult()
{
    static const mcstudio::AssociationResult empty;
    if (!m_association)
        return empty;
    return *m_association;
}

QString MainWindow::referenceInfoForTexture(const QString &relativePath)
{
    if (!m_hasProject || relativePath.isEmpty()
        || !relativePath.contains(QLatin1String("/textures/"))) {
        return {};
    }

    if (!m_association) {
        if (m_inspectorPanel) {
            m_inspectorPanel->setAssociationHint(
                tr("正在后台解析模型关联，完成后自动更新。"));
        }
        return tr("解析中…");
    }

    const mcstudio::AssociationResult &result = associationResult();
    for (const mcstudio::TextureGroup &group : result.groups) {
        if (!group.textures.contains(relativePath))
            continue;

        QString modelLabel;
        if (group.modelSource == QLatin1String("reference"))
            modelLabel = tr("参考实例");
        else if (group.modelSource == QLatin1String("mixed"))
            modelLabel = tr("资源包 + 参考实例");
        else
            modelLabel = tr("资源包");

        const QString modName =
            group.modName.isEmpty() ? group.namespaceId : group.modName;
        if (m_inspectorPanel) {
            QString hint = tr("关联：%1 · %2\n模型来源：%3；材质来源：资源包")
                               .arg(modName, group.displayName, modelLabel);
            if (!group.missingTextures.isEmpty())
                hint += tr("\n缺失材质：%1").arg(group.missingTextures.size());
            m_inspectorPanel->setAssociationHint(hint);
        }
        return tr("模型：%1 · 材质：资源包").arg(modelLabel);
    }

    if (m_inspectorPanel) {
        m_inspectorPanel->setAssociationHint(
            tr("未关联到方块/物品模型（检查 blockstates / models，或开启参考回退）。"));
    }
    return tr("未关联");
}

QString MainWindow::buildAssociationReport()
{
    QString text;
    QTextStream stream(&text);
    stream << "MC Asset Studio association report\n";
    stream << "schema=1\n";
    stream << "project=" << m_project.name << '\n';
    stream << "packFormat=" << m_project.packFormat << '\n';
    stream << "supportedFormats=";
    for (int i = 0; i < m_project.supportedFormats.size(); ++i) {
        if (i > 0)
            stream << ',';
        stream << m_project.supportedFormats.at(i);
    }
    stream << '\n';
    stream << "namespaces=" << m_projectNamespaces.size() << '\n';
    stream << "reference=" << m_referenceMatch.instanceName << '\n';
    stream << "referenceMode=" << m_referenceMatch.mode << '\n';
    stream << "compatible=" << (m_referenceMatch.compatible ? "yes" : "no") << '\n';
    stream << "referenceWarning=" << m_referenceMatch.warning << '\n';
    stream << "layers=" << (m_assetStack ? m_assetStack->providerCount() : 0) << '\n';
    if (m_assetStack) {
        for (const QString &label : m_assetStack->providerLabels())
            stream << "layer=" << label << '\n';
    }
    for (const QString &diagnostic : m_stackDiagnostics)
        stream << "diagnostic=" << diagnostic << '\n';

    const mcstudio::AssociationResult &result = associationResult();
    stream << "groups=" << result.groups.size() << '\n';
    stream << "blockGroups=" << result.blockGroups << '\n';
    stream << "itemGroups=" << result.itemGroups << '\n';
    stream << "referenceGroups=" << result.referenceGroups << '\n';
    stream << "missingModels=" << result.missingModels.size() << '\n';
    stream << "missingTextures=" << result.missingTextures.size() << '\n';
    stream << "buildMs=" << result.buildMs << '\n';
    stream << "readMs=" << result.readMs << '\n';
    stream << "jsonReads=" << result.jsonReads << '\n';

    for (const mcstudio::TextureGroup &group : result.groups) {
        stream << "\n[group]\n";
        stream << "id=" << group.id << '\n';
        stream << "kind=" << group.kind << '\n';
        stream << "namespace=" << group.namespaceId << '\n';
        stream << "mod=" << group.modName << '\n';
        stream << "display=" << group.displayName << '\n';
        stream << "modelSource=" << group.modelSource << '\n';
        stream << "textureSource=" << group.textureSource << '\n';
        stream << "textures=" << group.textures.size() << '\n';
        for (const QString &texture : group.textures)
            stream << "texture=" << texture << '\n';
        for (const QString &missing : group.missingTextures)
            stream << "missingTexture=" << missing << '\n';
    }

    for (const QString &model : result.missingModels)
        stream << "missingModel=" << model << '\n';
    for (const QString &texture : result.missingTextures)
        stream << "missingPackTexture=" << texture << '\n';
    return text;
}



void MainWindow::openTextTab(const QString &relativePath)
{
    if (!m_hasProject || !m_project.exists(relativePath)) {
        statusBar()->showMessage(tr("文件不存在：%1").arg(relativePath), 4000);
        return;
    }

    const QByteArray fileData = m_project.readBytes(relativePath);
    auto *document = new mcstudio::TextDocumentWidget(
        QFileInfo(relativePath).fileName(), relativePath, fileData);
    m_documentArea->openDocument(
        relativePath, QFileInfo(relativePath).fileName(),
        IconFactory::icon(IconFactory::Glyph::Info,
                          QColor(QStringLiteral("#94a3b8")), 16),
        document, relativePath);
}

void MainWindow::openImageTab(const QString &relativePath)
{
    if (!m_hasProject) {
        statusBar()->showMessage(tr("文件不存在：%1").arg(relativePath), 4000);
        return;
    }

    const bool inProject = m_project.exists(relativePath);
    if (!inProject
        && (!m_assetStack || !m_assetStack->exists(relativePath))) {
        statusBar()->showMessage(tr("文件不存在：%1").arg(relativePath), 4000);
        return;
    }

    const QByteArray fileData = inProject ? m_project.readBytes(relativePath)
                                          : m_assetStack->read(relativePath);
    const QString title =
        QFileInfo(relativePath).fileName()
        + (inProject ? QString() : tr("（参考）"));
    auto *document = new mcstudio::ImageDocumentWidget(title, relativePath, fileData);
    m_documentArea->openDocument(
        relativePath, title,
        IconFactory::icon(IconFactory::Glyph::Image,
                          QColor(inProject ? Theme::colors().accent
                                           : Theme::colors().reference),
                          16),
        document, relativePath);
}

void MainWindow::openFileTab(const QString &relativePath)
{
    if (relativePath.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
        openImageTab(relativePath);
    else
        openTextTab(relativePath);
}

mcstudio::TextureEditorPage *MainWindow::activeTextureEditor() const
{
    for (auto it = m_textureWindows.constBegin(); it != m_textureWindows.constEnd(); ++it) {
        if (it.value() && it.value()->isActiveWindow() && it.value()->page())
            return it.value()->page();
    }

    if (!m_documentArea)
        return nullptr;
    const QString key = m_documentArea->currentDocumentKey();
    if (!key.startsWith(QLatin1String("edit:")))
        return nullptr;
    return m_textureEditors.value(key).data();
}

void MainWindow::updateEditorActions()
{
    if (m_shuttingDown)
        return;
    auto *editor = activeTextureEditor();
    QWidget *focus = QApplication::focusWidget();
    const bool textFocus = qobject_cast<QLineEdit *>(focus)
                           || qobject_cast<QAbstractSpinBox *>(focus)
                           || qobject_cast<QComboBox *>(focus);
    if (m_undoAction)
        m_undoAction->setEnabled(editor && editor->canUndo() && !textFocus);
    if (m_redoAction)
        m_redoAction->setEnabled(editor && editor->canRedo() && !textFocus);
    if (m_saveAction)
        m_saveAction->setEnabled(editor && editor->isDirty());
}

bool MainWindow::confirmCloseTextureEditors(const QString &context)
{
    QStringList dirty;
    for (auto it = m_textureEditors.constBegin(); it != m_textureEditors.constEnd(); ++it) {
        if (it.value() && it.value()->isDirty())
            dirty.append(it.value()->relativePath());
    }
    if (dirty.isEmpty())
        return true;

    QString preview = dirty.mid(0, 8).join(QLatin1Char('\n'));
    if (dirty.size() > 8)
        preview += tr("\n… 以及另外 %1 个").arg(dirty.size() - 8);

    const auto answer = QMessageBox::warning(
        this, tr("未保存的材质"),
        tr("%1 个材质编辑标签有未保存修改：\n\n%2\n\n是否保存全部后再%3？")
            .arg(dirty.size())
            .arg(preview, context),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Discard)
        return true;

    for (auto it = m_textureEditors.constBegin(); it != m_textureEditors.constEnd(); ++it) {
        if (it.value() && it.value()->isDirty()) {
            if (!it.value()->saveDocument())
                return false;
        }
    }
    return true;
}

bool MainWindow::openTextureEditor(const QString &relativePath)
{
    if (!m_hasProject) {
        statusBar()->showMessage(tr("请先打开项目。"), 4000);
        return false;
    }

    QString path = QDir::fromNativeSeparators(relativePath);
    while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    if (path.isEmpty())
        return false;

    if (!m_project.exists(path)) {
        if (!m_assetStack || !m_assetStack->exists(path)) {
            statusBar()->showMessage(tr("材质不存在：%1").arg(path), 4000);
            return false;
        }

        const auto answer = QMessageBox::question(
            this, tr("编辑参考材质"),
            tr("“%1”来自参考实例，不在资源包内。\n\n"
               "是否先复制到 pack/ 覆盖层，再打开编辑器？")
                .arg(path),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (answer != QMessageBox::Yes)
            return false;

        if (!importReferenceTexture(path) || !m_project.exists(path))
            return false;
    }

    const QString key = QStringLiteral("edit:") + path;
    if (auto existing = m_textureEditors.value(key)) {
        if (auto window = m_textureWindows.value(key)) {
            window->show();
            window->raise();
            window->activateWindow();
        } else {
            m_documentArea->activateDocument(key);
        }
        existing->focusCanvas();
        return true;
    }

    auto *page = new mcstudio::TextureEditorPage(m_project, path);
    m_textureEditors.insert(key, page);

    page->onSaved = [this, path] { onTextureEdited(path); };
    page->onDirtyChanged = [this, key, page](bool dirty) {
        if (m_documentArea && m_documentArea->hasDocument(key)) {
            QString title = QFileInfo(page->relativePath()).fileName();
            if (dirty)
                title += QStringLiteral(" *");
            m_documentArea->setDocumentTitle(key, title);
        }
        updateTextureWindowTitle(key);
        updateEditorActions();
    };
    page->onHistoryChanged = [this] { updateEditorActions(); };
    page->onPopOut = [this, key] { popOutTextureEditor(key); };
    page->onReturnToTab = [this, key] { returnTextureEditorToTab(key); };
    page->onFocusModeChanged = [this](bool enabled) {
        applyEditorFocusMode(enabled);
    };
    page->onFilesGenerated = [this](const QStringList &paths) {
        for (const QString &generated : paths)
            onTextureEdited(generated);
    };
    page->referencePathProvider = [this](bool includeReference) {
        return referenceTexturePaths(includeReference);
    };
    page->referenceBytesProvider = [this](const QString &referencePath) {
        return readReferenceTexture(referencePath);
    };

    QObject::connect(page, &QObject::destroyed, this, [this, key] {
        if (m_shuttingDown)
            return;
        m_textureEditors.remove(key);
        updateEditorActions();
    });

    const QString title = QFileInfo(path).fileName();
    m_documentArea->openDocument(
        key, title,
        IconFactory::icon(IconFactory::Glyph::Image,
                          Theme::colors().accent, 16),
        page, path);
    page->focusCanvas();
    updateEditorActions();
    return true;
}

void MainWindow::setTextureEditorReferenceForTesting(const QString &path)
{
    auto *editor = activeTextureEditor();
    if (!editor)
        return;
    const QByteArray bytes = readReferenceTexture(path);
    const QImage image = QImage::fromData(bytes);
    if (!image.isNull())
        editor->setReferenceImage(image, QFileInfo(path).fileName());
}

void MainWindow::popOutActiveTextureEditorForTesting()
{
    auto *editor = activeTextureEditor();
    if (!editor)
        return;
    for (auto it = m_textureEditors.constBegin(); it != m_textureEditors.constEnd(); ++it) {
        if (it.value() == editor) {
            popOutTextureEditor(it.key());
            return;
        }
    }
}

void MainWindow::returnActiveTextureEditorForTesting()
{
    QString targetKey;
    auto *editor = activeTextureEditor();
    if (editor) {
        for (auto it = m_textureEditors.constBegin(); it != m_textureEditors.constEnd(); ++it) {
            if (it.value() == editor) {
                targetKey = it.key();
                break;
            }
        }
    }
    if (targetKey.isEmpty() && !m_textureWindows.isEmpty())
        targetKey = m_textureWindows.constBegin().key();
    if (!targetKey.isEmpty())
        returnTextureEditorToTab(targetKey);
}

void MainWindow::setTextureEditorFocusForTesting(bool enabled)
{
    applyEditorFocusMode(enabled);
    if (auto *editor = activeTextureEditor())
        editor->setFocusMode(enabled);
}

void MainWindow::applyHalfMaskTintForTesting()
{
    if (auto *editor = activeTextureEditor())
        editor->applyHalfMaskTintForTesting();
}

void MainWindow::applyHalfMaskForTesting()
{
    if (auto *editor = activeTextureEditor())
        editor->applyHalfMaskForTesting();
}

void MainWindow::openMergeDialogForTesting()
{
    if (auto *editor = activeTextureEditor())
        editor->showMergeDialogForTesting();
}

bool MainWindow::generateEquipmentForTesting()
{
    if (auto *editor = activeTextureEditor())
        return editor->generateBuiltinEquipmentForTesting();
    return false;
}

void MainWindow::openEquipmentDialogForTesting()
{
    if (auto *editor = activeTextureEditor())
        editor->showEquipmentDialogForTesting();
}

void MainWindow::openTemplateDialogForTesting()
{
    if (auto *editor = activeTextureEditor())
        editor->showTemplateDialogForTesting();
}

void MainWindow::openTintDialogForTesting()
{
    if (auto *editor = activeTextureEditor())
        editor->showTintDialogForTesting();
}

void MainWindow::openReduceDialogForTesting()
{
    if (auto *editor = activeTextureEditor())
        editor->showReduceDialogForTesting();
}

void MainWindow::applyBuiltinTintForTesting()
{
    if (auto *editor = activeTextureEditor())
        editor->applyBuiltinTestTint();
}

bool MainWindow::saveActiveTextureEditorForTesting()
{
    if (auto *editor = activeTextureEditor())
        return editor->saveDocument();
    return false;
}

QWidget *MainWindow::activeTextureEditorWindowForTesting() const
{
    if (auto *editor = activeTextureEditor())
        return editor->window();
    for (auto it = m_textureEditors.constBegin(); it != m_textureEditors.constEnd(); ++it) {
        if (it.value())
            return it.value()->window();
    }
    return nullptr;
}

void MainWindow::popOutTextureEditor(const QString &key)
{
    auto page = m_textureEditors.value(key);
    if (!page)
        return;
    if (auto existing = m_textureWindows.value(key)) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    QWidget *content = m_documentArea ? m_documentArea->takeDocument(key) : nullptr;
    if (!content)
        return;

    auto *window = new mcstudio::TextureEditorWindow(page, nullptr);
    window->onCloseRequested = [this, key] {
        return requestCloseTextureEditorWindow(key);
    };
    m_textureWindows.insert(key, window);
    page->setWindowed(true);

    QSettings settings = appSettings();
    const QByteArray geometry =
        settings.value(QStringLiteral("window/editorGeometry")).toByteArray();
    if (!geometry.isEmpty())
        window->restoreGeometry(geometry);

    window->show();
    window->raise();
    window->activateWindow();
    page->show();
    page->focusCanvas();
    updateEditorActions();
}

void MainWindow::returnTextureEditorToTab(const QString &key)
{
    auto page = m_textureEditors.value(key);
    auto window = m_textureWindows.take(key);
    if (!page) {
        if (window)
            window->deleteLater();
        return;
    }

    if (window) {
        QSettings settings = appSettings();
        settings.setValue(QStringLiteral("window/editorGeometry"),
                          window->saveGeometry());
        window->setAllowClose(true);
    }

    page->setParent(nullptr);
    m_documentArea->openDocument(
        key, QFileInfo(page->relativePath()).fileName(),
        IconFactory::icon(IconFactory::Glyph::Image, Theme::colors().accent, 16),
        page, page->relativePath());
    page->setWindowed(false);
    m_documentArea->activateDocument(key);
    page->show();

    if (window) {
        window->hide();
        window->deleteLater();
    }
    page->focusCanvas();
    updateEditorActions();
}

bool MainWindow::requestCloseTextureEditorWindow(const QString &key)
{
    auto page = m_textureEditors.value(key);
    if (!page)
        return true;

    if (page->isDirty()) {
        const auto answer = QMessageBox::warning(
            this, tr("未保存的材质"),
            tr("“%1”有未保存的修改。").arg(page->relativePath()),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (answer == QMessageBox::Cancel)
            return false;
        if (answer == QMessageBox::Save && !page->saveDocument())
            return false;
    }

    auto window = m_textureWindows.take(key);
    if (window) {
        QSettings settings = appSettings();
        settings.setValue(QStringLiteral("window/editorGeometry"),
                          window->saveGeometry());
        window->setAllowClose(true);
        window->hide();
        window->deleteLater();
    }
    m_textureEditors.remove(key);
    updateEditorActions();
    return false;
}

void MainWindow::closeAllTextureWindows()
{
    const QStringList keys = m_textureWindows.keys();
    for (const QString &key : keys) {
        auto window = m_textureWindows.take(key);
        if (!window)
            continue;
        QSettings settings = appSettings();
        settings.setValue(QStringLiteral("window/editorGeometry"),
                          window->saveGeometry());
        window->setAllowClose(true);
        window->hide();
        window->deleteLater();
        m_textureEditors.remove(key);
    }
}

void MainWindow::updateTextureWindowTitle(const QString &key)
{
    auto window = m_textureWindows.value(key);
    auto page = m_textureEditors.value(key);
    if (!window || !page)
        return;

    QString title = tr("材质编辑 — %1").arg(QFileInfo(page->relativePath()).fileName());
    if (page->isDirty())
        title += QStringLiteral(" *");
    window->setWindowTitle(title);
}

void MainWindow::applyEditorFocusMode(bool enabled)
{
    if (m_editorFocusMode == enabled)
        return;
    m_editorFocusMode = enabled;
    if (!m_toggleSidebarAction || !m_toggleInspectorAction)
        return;

    if (enabled) {
        m_focusSidebarWasVisible = m_toggleSidebarAction->isChecked();
        m_focusInspectorWasVisible = m_toggleInspectorAction->isChecked();
        m_toggleSidebarAction->setChecked(false);
        m_toggleInspectorAction->setChecked(false);
    } else {
        m_toggleSidebarAction->setChecked(m_focusSidebarWasVisible);
        m_toggleInspectorAction->setChecked(m_focusInspectorWasVisible);
    }
}

QStringList MainWindow::referenceTexturePaths(bool includeReference) const
{
    QSet<QString> unique;
    if (m_hasProject) {
        const auto &index = m_project.fileIndex();
        for (auto it = index.constBegin(); it != index.constEnd(); ++it) {
            if (it.value().isTexture
                && it.key().endsWith(QLatin1String(".png"), Qt::CaseInsensitive)) {
                unique.insert(it.key());
            }
        }
    }
    if (includeReference && m_assetStack) {
        const QStringList paths = m_assetStack->list(QStringLiteral("assets/"));
        for (const QString &path : paths) {
            if (path.endsWith(QLatin1String(".png"), Qt::CaseInsensitive))
                unique.insert(path);
        }
    }

    QStringList result;
    result.reserve(unique.size());
    for (const QString &path : unique)
        result.append(path);
    result.sort(Qt::CaseInsensitive);
    return result;
}

QByteArray MainWindow::readReferenceTexture(const QString &path) const
{
    if (m_project.exists(path))
        return m_project.readBytes(path);
    if (m_assetStack) {
        const QByteArray bytes = m_assetStack->read(path);
        if (!bytes.isEmpty())
            return bytes;
    }
    // Fallback for the default template import: if no reference instance is
    // selected, read vanilla assets straight from any scanned client jar.
    if (path.startsWith(QLatin1String("assets/minecraft/"))) {
        for (const mcstudio::GameRootInfo &root : m_environment.roots) {
            for (const mcstudio::InstanceInfo &instance : root.instances) {
                if (!instance.hasClientJar || instance.clientJar.isEmpty())
                    continue;
                mcstudio::ZipArchive jar(instance.clientJar);
                if (!jar.isOpen())
                    continue;
                const QByteArray bytes = jar.read(path);
                if (!bytes.isEmpty())
                    return bytes;
            }
        }
    }
    return {};
}

void MainWindow::onTextureEdited(const QString &relativePath){
    if (!m_hasProject)
        return;

    m_project.scan();
    if (m_textureBoardPage)
        m_textureBoardPage->replaceTexture(m_project, relativePath);
    rebuildCategoryIndex();
    updateCategoryCounts();
    if (m_selectedTexturePath == relativePath)
        showTextureInInspector(relativePath);
    updateProjectUi();
    statusBar()->showMessage(tr("已保存材质：%1").arg(relativePath), 4000);
}

void MainWindow::restoreProjectSession()
{
    bool restored = false;

    QFile session(mcstudio::AppPaths::sessionFile(m_project.id));
    if (session.open(QIODevice::ReadOnly)) {
        const QJsonDocument document = QJsonDocument::fromJson(session.readAll());
        if (document.isObject()) {
            const QJsonObject object = document.object();
            const QJsonArray tabs = object.value(QStringLiteral("openTabs")).toArray();
            for (const QJsonValue &value : tabs) {
                const QString path = value.toString();
                if (!path.isEmpty() && m_project.exists(path)) {
                    openFileTab(path);
                    restored = true;
                }
            }

            const QString current = object.value(QStringLiteral("current")).toString();
            if (!current.isEmpty() && m_documentArea->hasDocument(current))
                m_documentArea->activateDocument(current);
            else
                m_documentArea->activateDocument(QStringLiteral("__overview__"));
        }
    }

    if (!restored) {
        if (m_project.exists(QStringLiteral("pack.mcmeta")))
            openTextTab(QStringLiteral("pack.mcmeta"));
        if (m_project.exists(QStringLiteral("pack.png")))
            openImageTab(QStringLiteral("pack.png"));
    }

    if (m_documentArea->count() == 1)
        m_documentArea->activateDocument(QStringLiteral("__overview__"));
}

void MainWindow::saveProjectSession()
{
    if (!m_hasProject)
        return;

    QDir().mkpath(mcstudio::AppPaths::sessionsRoot());

    QJsonArray tabs;
    for (const QString &path : m_documentArea->documentPaths())
        tabs.append(path);

    QJsonObject object;
    object.insert(QStringLiteral("openTabs"), tabs);
    object.insert(QStringLiteral("current"), m_documentArea->currentDocumentPath());
    object.insert(QStringLiteral("savedAt"),
                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSaveFile file(mcstudio::AppPaths::sessionFile(m_project.id));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    file.commit();
}

void MainWindow::openDataFolder()
{
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(mcstudio::AppPaths::dataRoot()));
}

void MainWindow::openProjectFolder()
{
    if (m_hasProject && !m_project.rootDir.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_project.rootDir));
        return;
    }
    openDataFolder();
}

void MainWindow::activateTab(const QString &key)
{
    if (m_documentArea)
        m_documentArea->activateDocument(key);
}

void MainWindow::exportProjectDialog(bool overwrite)
{
    if (!m_hasProject)
        return;

    QString target;
    if (overwrite) {
        if (m_project.sourceZip.isEmpty()) {
            QMessageBox::information(
                this, tr("无法覆盖原 ZIP"),
                tr("这个项目是新建或由文件夹导入的，没有原始 ZIP。\n"
                   "请使用“导出为新 ZIP”。"));
            return;
        }
        const auto answer = QMessageBox::warning(
            this, tr("覆盖原 ZIP"),
            tr("将用当前项目内容原子替换原 ZIP：\n%1\n\n继续？")
                .arg(m_project.sourceZip),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        target = m_project.sourceZip;
    } else {
        const QString defaultPath =
            QDir(m_project.rootDir).filePath(m_project.name + QStringLiteral(".zip"));
        target = QFileDialog::getSaveFileName(
            this, tr("导出资源包 ZIP"), defaultPath,
            tr("Minecraft 资源包 (*.zip);;所有文件 (*.*)"));
        if (target.isEmpty())
            return;
        if (!target.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
            target += QStringLiteral(".zip");
    }

    QString error;
    if (!exportProject(target, overwrite, &error)) {
        QMessageBox::critical(this, tr("导出失败"), error);
        return;
    }

    QMessageBox::information(
        this, tr("导出完成"),
        overwrite ? tr("已覆盖原 ZIP：\n%1").arg(target)
                  : tr("已导出：\n%1").arg(target));
}

bool MainWindow::exportProject(const QString &targetZip, bool overwrite, QString *error)
{
    if (!m_hasProject) {
        if (error)
            *error = tr("当前没有打开的项目");
        return false;
    }

    const mcstudio::ExportResult result =
        mcstudio::ExportService::exportZip(m_project, targetZip);
    if (!result.ok) {
        if (error)
            *error = result.error;
        statusBar()->showMessage(tr("导出失败：%1").arg(result.error), 6000);
        return false;
    }

    statusBar()->showMessage(
        (overwrite ? tr("已覆盖原 ZIP：%1") : tr("已导出：%1")).arg(targetZip),
        6000);
    return true;
}

void MainWindow::updateBreadcrumb()
{
    if (!m_breadcrumb || !m_categoryTree)
        return;

    QStringList parts;
    if (!m_namespaceFilter.isEmpty())
        parts << currentModLabel();

    QTreeWidgetItem *item = m_categoryTree->currentItem();
    if (item && item->data(0, Qt::UserRole).toString() != QLatin1String("all")) {
        for (QTreeWidgetItem *current = item; current; current = current->parent())
            parts.append(current->text(0));
    }

    if (parts.isEmpty())
        m_breadcrumb->setText(tr("资源浏览"));
    else
        m_breadcrumb->setText(tr("资源浏览 / %1").arg(parts.join(QStringLiteral(" / "))));
}

void MainWindow::openPreferencesDialog()
{
    mcstudio::PreferencesDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_gameSettings = mcstudio::loadGameSettings();
    updateReferenceLabel();
    if (m_textureBoardPage)
        m_textureBoardPage->setShowReferenceTextures(m_gameSettings.showReferenceTextures);
    if (m_hasProject) {
        startModCatalogForProject();
    } else {
        m_environment = mcstudio::GameEnvironment::loadCachedSnapshot(m_gameSettings.roots);
        updateReferenceLabel();
    }

    if (m_environment.roots.isEmpty()) {
        statusBar()->showMessage(tr("游戏环境：尚未扫描游戏目录"), 6000);
        return;
    }

    statusBar()->showMessage(
        tr("游戏环境：%1 个根目录 · %2 个实例 · %3 个 mod · %4 个资源包")
            .arg(m_environment.roots.size())
            .arg(m_environment.instanceCount())
            .arg(m_environment.modCount())
            .arg(m_environment.resourcePackCount()),
        8000);
}

QString MainWindow::buildDiagnosticsReport()
{
    if (!m_hasProject)
        return tr("请先导入一个资源包。");
    if (!m_association)
        return tr("模型关联仍在后台解析，完成后再查看诊断。");

    QSet<QString> associated;
    int packGroups = 0;
    int referenceGroups = 0;
    int mixedGroups = 0;
    for (const mcstudio::TextureGroup &group : m_association->groups) {
        for (const QString &texture : group.textures) {
            associated.insert(texture);
        }
        if (group.modelSource == QLatin1String("reference"))
            ++referenceGroups;
        else if (group.modelSource == QLatin1String("mixed"))
            ++mixedGroups;
        else
            ++packGroups;
    }

    QStringList unassociated;
    if (m_textureBoardPage) {
        for (const mcstudio::TextureBoardEntry &entry : m_textureBoardPage->entries()) {
            if (entry.path.isEmpty() || associated.contains(entry.path))
                continue;
            unassociated.append(entry.path);
        }
    }

    QStringList missingMods;
    if (m_modCatalog) {
        for (const mcstudio::NamespaceMod &mapping : m_modCatalog->entries()) {
            if (!mapping.found)
                missingMods.append(mapping.namespaceId);
        }
    }

    QSet<QString> uniqueModels;
    for (const QString &model : m_association->missingModels)
        uniqueModels.insert(model);

    QString text;
    QTextStream stream(&text);
    stream << "项目：" << m_project.name << '\n';
    stream << "pack_format：" << m_project.packFormat << '\n';
    stream << "命名空间：" << m_projectNamespaces.size() << '\n';
    if (m_referenceMatch.found) {
        stream << "参考实例：" << m_referenceMatch.instanceName
               << "（" << m_referenceMatch.clientVersion << " · "
               << mcstudio::loaderDisplayName(m_referenceMatch.loader) << "）\n";
        stream << "匹配方式：" << m_referenceMatch.mode << " · "
               << (m_referenceMatch.compatible ? "兼容" : "不完全匹配") << '\n';
    } else {
        stream << "参考实例：未选择\n";
    }
    if (m_assetStack)
        stream << "资源层：" << m_assetStack->providerCount() << " 个\n";

    stream << "\n模型关联：" << m_association->groups.size() << " 组"
           << "（方块 " << m_association->blockGroups << " / 物品 "
           << m_association->itemGroups << "）\n";
    stream << "模型来源：资源包 " << packGroups << " · 参考 " << referenceGroups
           << " · 混合 " << mixedGroups << '\n';
    stream << "缺失模型：" << uniqueModels.size() << '\n';
    stream << "缺失材质：" << m_association->missingTextures.size() << '\n';
    stream << "未关联材质：" << unassociated.size() << '\n';
    stream << "未找到模组：" << missingMods.size() << '\n';

    QStringList scanErrors;
    if (m_modCatalog) {
        for (const QString &error : m_modCatalog->errors())
            scanErrors.append(error);
    }
    stream << "模组扫描错误：" << scanErrors.size() << '\n';

    const auto appendList = [&stream](const QString &title, const QStringList &values,
                                      int limit) {
        stream << '\n' << title << "（" << values.size() << "）：\n";
        const int shown = qMin(values.size(), limit);
        for (int i = 0; i < shown; ++i)
            stream << "  " << values.at(i) << '\n';
        if (values.size() > shown)
            stream << "  …\n";
    };

    QStringList sortedModels = m_association->missingModels;
    sortedModels.sort(Qt::CaseInsensitive);
    appendList(tr("未找到模组"), missingMods, 100);
    appendList(tr("模组扫描错误"), scanErrors, 100);
    appendList(tr("缺失模型"), sortedModels, 200);
    appendList(tr("缺失材质（参考模型引用但资源包没有）"),
               m_association->missingTextures, 200);
    appendList(tr("未关联材质（资源包内没有对应方块/物品模型）"), unassociated, 500);

    return text;
}

void MainWindow::showAssociationDiagnostics()
{
    if (!m_hasProject) {
        QMessageBox::information(this, tr("关联诊断"), tr("请先导入一个资源包。"));
        return;
    }
    if (!m_association) {
        QMessageBox::information(this, tr("关联诊断"),
                                 tr("模型关联仍在后台解析，完成后再打开诊断。"));
        return;
    }

    const QString text = buildDiagnosticsReport();

    QDialog dialog(this);
    dialog.setWindowTitle(tr("关联诊断"));
    dialog.resize(840, 620);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(12, 12, 12, 10);
    layout->setSpacing(8);

    auto *heading = new QLabel(tr("关联诊断"));
    heading->setObjectName(QStringLiteral("PanelTitle"));
    layout->addWidget(heading);

    auto *view = new QPlainTextEdit(text);
    view->setReadOnly(true);
    view->setObjectName(QStringLiteral("DocumentText"));
    layout->addWidget(view, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    if (QPushButton *close = buttons->button(QDialogButtonBox::Close))
        close->setText(tr("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog.exec();
}

void MainWindow::showNotImplemented(const QString &feature)
{
    statusBar()->showMessage(
        tr("「%1」尚未接入 · 当前为界面骨架").arg(feature), 4000);

    if (m_noticeShown.contains(feature))
        return;
    m_noticeShown.insert(feature);

    QMessageBox box(this);
    box.setWindowTitle(tr("界面骨架"));
    box.setIcon(QMessageBox::Information);
    box.setText(tr("「%1」尚未接入").arg(feature));
    box.setInformativeText(
        tr("当前已接入 ZIP 自动解包、多标签预览、导出与覆盖原 ZIP。"
           "材质编辑器、资源索引持久化和动画时间轴仍待接入。"));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("关于 MC Asset Studio"),
        tr("<h3>MC Asset Studio</h3>"
           "<p>版本 %1 · 界面骨架</p>"
           "<p>面向 Minecraft Java Edition 的资源包工作台。<br>"
           "当前版本支持 ZIP 自动解包、多标签预览、导出与覆盖原 ZIP。</p>"
           "<p style=\"color:#8b95a7\">%2</p>"
           "<p style=\"color:#8b95a7\">与 Mojang / Microsoft 无隶属关系。</p>")
            .arg(mcstudio::version(), mcstudio::buildSummary()));
}

void MainWindow::resetLayout()
{
    if (!m_splitter)
        return;

    m_toggleSidebarAction->setChecked(true);
    m_toggleInspectorAction->setChecked(true);
    m_sidebar->show();
    m_inspector->show();
    m_splitter->setSizes(QList<int>{260, 900, 320});
}

void MainWindow::restoreWindowState()
{
    QSettings settings = appSettings();

    const QByteArray geometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    } else {
        resize(1440, 900);
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            const QRect available = screen->availableGeometry();
            move(available.center() - rect().center());
        }
    }

    const QByteArray state = settings.value(QStringLiteral("window/state")).toByteArray();
    if (!state.isEmpty())
        restoreState(state);

    const QByteArray splitterState =
        settings.value(QStringLiteral("window/splitter")).toByteArray();
    if (!splitterState.isEmpty() && m_splitter)
        m_splitter->restoreState(splitterState);

    const bool sidebarVisible =
        settings.value(QStringLiteral("window/sidebarVisible"), true).toBool();
    const bool inspectorVisible =
        settings.value(QStringLiteral("window/inspectorVisible"), true).toBool();
    m_toggleSidebarAction->setChecked(sidebarVisible);
    m_toggleInspectorAction->setChecked(inspectorVisible);
}

void MainWindow::saveWindowState()
{
    QSettings settings = appSettings();
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    if (m_splitter)
        settings.setValue(QStringLiteral("window/splitter"), m_splitter->saveState());
    settings.setValue(QStringLiteral("window/sidebarVisible"), m_sidebar->isVisible());
    settings.setValue(QStringLiteral("window/inspectorVisible"), m_inspector->isVisible());
}

QAction *MainWindow::makeAction(const QString &text, IconFactory::Glyph glyph,
                                const QKeySequence &shortcut)
{
    auto *action = new QAction(
        IconFactory::icon(glyph, QColor(QStringLiteral("#94a3b8")), 18), text, this);
    if (!shortcut.isEmpty())
        action->setShortcut(shortcut);
    action->setToolTip(text);
    action->setStatusTip(text);
    return action;
}
