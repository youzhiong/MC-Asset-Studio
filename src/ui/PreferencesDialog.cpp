#include "ui/PreferencesDialog.h"

#include "core/AppPaths.h"
#include "core/GameEnvironment.h"
#include "ui/GameDirectoryPage.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace mcstudio {
namespace {

QFrame *makeDivider()
{
    auto *divider = new QFrame;
    divider->setObjectName(QStringLiteral("Divider"));
    divider->setFixedHeight(1);
    return divider;
}

QString formatBytes(qint64 bytes)
{
    if (bytes >= 1024 * 1024) {
        return QStringLiteral("%1 MB")
            .arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    }
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

} // namespace

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("首选项"));
    setModal(true);
    resize(880, 660);
    setMinimumSize(760, 560);
    buildUi();

    // Load current settings, then refresh the environment in the background
    // (cache fast-path) so the list is live as soon as the dialog opens.
    const GameSettings current = loadGameSettings();
    m_preferredStyle = Theme::normalizeStyleId(current.widgetStyle);
    m_gamePage->setSettings(current);
    if (m_fallbackModelsCheck)
        m_fallbackModelsCheck->setChecked(current.fallbackModels);
    if (m_fallbackLangCheck)
        m_fallbackLangCheck->setChecked(current.fallbackLang);
    if (m_showReferenceCheck)
        m_showReferenceCheck->setChecked(current.showReferenceTextures);
    if (m_themeCombo) {
        const int index = m_themeCombo->findData(Theme::normalizeId(current.theme));
        if (index >= 0)
            m_themeCombo->setCurrentIndex(index);
        updateThemePreview();
    }
    if (m_styleCombo) {
        const int index = m_styleCombo->findData(m_preferredStyle);
        if (index >= 0)
            m_styleCombo->setCurrentIndex(index);
    }
    updateStyleForTheme();
    m_gamePage->startScan(false);
    refreshCacheSummary();
}

void PreferencesDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    m_tabs = tabs;

    m_gamePage = new GameDirectoryPage;
    m_gamePage->onScanFinished = [this] { refreshCacheSummary(); };
    m_gamePage->onSettingsChanged = [this] {
        if (m_buttons) {
            if (QPushButton *apply = m_buttons->button(QDialogButtonBox::Apply))
                apply->setEnabled(true);
        }
    };
    tabs->addTab(m_gamePage, tr("游戏目录"));
    tabs->addTab(createReferencePage(), tr("资源与回退"));
    tabs->addTab(createCachePage(), tr("缓存"));
    tabs->addTab(createAppearancePage(), tr("外观"));
    layout->addWidget(tabs, 1);

    layout->addWidget(makeDivider());

    auto *footer = new QWidget;
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(18, 10, 18, 12);
    footerLayout->setSpacing(8);

    auto *hint = new QLabel(
        tr("设置和缓存位于 %1，项目位于 %2；不会写入资源包 ZIP。")
            .arg(QDir::toNativeSeparators(AppPaths::dataRoot()),
                 QDir::toNativeSeparators(AppPaths::projectsRoot())));
    hint->setObjectName(QStringLiteral("PanelHint"));

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    m_buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    m_buttons->button(QDialogButtonBox::Apply)->setText(tr("应用"));
    if (QPushButton *apply = m_buttons->button(QDialogButtonBox::Apply))
        apply->setObjectName(QStringLiteral("GhostButton"));
    if (QPushButton *ok = m_buttons->button(QDialogButtonBox::Ok))
        ok->setObjectName(QStringLiteral("PrimaryButton"));

    footerLayout->addWidget(hint);
    footerLayout->addStretch();
    footerLayout->addWidget(m_buttons);
    layout->addWidget(footer);

    connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttons, &QDialogButtonBox::clicked, this, [this](QAbstractButton *button) {
        if (m_buttons->buttonRole(button) == QDialogButtonBox::ApplyRole) {
            save();
            refreshCacheSummary();
        }
    });
}

QWidget *PreferencesDialog::createAppearancePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(9);

    auto *title = new QLabel(tr("外观"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    layout->addWidget(title);

    auto *hint = new QLabel(
        tr("主题会应用到主窗口、材质墙、集合模式和像素编辑器。选择后点“应用”或“确定”生效。"));
    hint->setObjectName(QStringLiteral("PanelHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *themeLabel = new QLabel(tr("配色"));
    themeLabel->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(themeLabel);

    m_themeCombo = new QComboBox;
    m_themeCombo->setMinimumWidth(260);
    m_themeCombo->addItem(tr("跟随系统（自动深/浅）"), QStringLiteral("auto"));
    m_themeCombo->addItem(tr("Windows 11 浅色（原生样式）"), QStringLiteral("win11light"));
    m_themeCombo->addItem(tr("Windows 11（原生样式）"), QStringLiteral("win11"));
    for (const Theme::Info &info : Theme::availableThemes())
        m_themeCombo->addItem(info.name, info.id);
    m_themeCombo->setToolTip(tr("跟随系统会按 Windows 深色/浅色设置自动选择曜石黑或纯白"));
    connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updateThemePreview();
        updateStyleForTheme();
    });
    layout->addWidget(m_themeCombo);

    auto *styleLabel = new QLabel(tr("控件样式"));
    styleLabel->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(styleLabel);

    m_styleCombo = new QComboBox;
    m_styleCombo->setMinimumWidth(260);
    for (const Theme::StyleInfo &info : Theme::availableStyles())
        m_styleCombo->addItem(info.name, info.id);
    m_styleCombo->setToolTip(tr("Qt 原生控件样式；Windows 11 最接近 WinUI"));
    connect(m_styleCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_styleCombo && m_styleCombo->isEnabled())
            m_preferredStyle = m_styleCombo->currentData().toString();
    });
    layout->addWidget(m_styleCombo);

    m_themePreview = new QFrame;
    m_themePreview->setObjectName(QStringLiteral("ThemePreview"));
    m_themePreview->setMinimumHeight(200);
    auto *previewLayout = new QVBoxLayout(m_themePreview);
    previewLayout->setContentsMargins(16, 14, 16, 14);
    previewLayout->setSpacing(6);

    m_themeName = new QLabel;
    m_themeName->setObjectName(QStringLiteral("EmptyTitle"));
    m_themeName->setStyleSheet(QStringLiteral("font-size: 18px;"));
    m_themeDescription = new QLabel;
    m_themeDescription->setObjectName(QStringLiteral("PanelHint"));
    m_themeDescription->setWordWrap(true);
    previewLayout->addWidget(m_themeName);
    previewLayout->addWidget(m_themeDescription);

    auto *swatchRow = new QHBoxLayout;
    swatchRow->setSpacing(8);
    const QStringList swatchNames = {tr("窗口"), tr("面板"), tr("输入区"), tr("强调"), tr("文字")};
    for (const QString &name : swatchNames) {
        auto *swatch = new QFrame;
        swatch->setFixedSize(54, 34);
        swatch->setToolTip(name);
        m_themeSwatches.append(swatch);
        swatchRow->addWidget(swatch);
    }
    swatchRow->addStretch(1);
    previewLayout->addLayout(swatchRow);

    auto *sampleRow = new QHBoxLayout;
    sampleRow->setSpacing(8);
    auto *sampleButton = new QPushButton(tr("按钮"));
    sampleButton->setObjectName(QStringLiteral("GhostButton"));
    auto *sampleCheck = new QCheckBox(tr("选项"));
    sampleCheck->setChecked(true);
    auto *sampleCombo = new QComboBox;
    sampleCombo->addItems({tr("下拉 A"), tr("下拉 B")});
    auto *sampleSpin = new QSpinBox;
    sampleSpin->setValue(12);
    sampleRow->addWidget(sampleButton);
    sampleRow->addWidget(sampleCheck);
    sampleRow->addWidget(sampleCombo);
    sampleRow->addWidget(sampleSpin);
    sampleRow->addStretch(1);
    previewLayout->addLayout(sampleRow);
    previewLayout->addStretch(1);
    layout->addWidget(m_themePreview);

    auto *note = new QLabel(
        tr("“Windows 11 浅色（原生样式）”是默认主题；另有跟随系统的 Windows 11 原生主题、"
           "曜石黑和纯白，以及蓝白、薄荷、樱花、薰衣草等浅色主题。"
           "Windows 11 主题会强制使用 Qt 的 windows11 平台样式，此时控件样式下拉锁定；"
           "其他主题仍可选择 Fusion / Windows 11 等控件样式。主题和样式只改变界面，不改变资源包内容。"));
    note->setObjectName(QStringLiteral("PanelHint"));
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch(1);

    updateThemePreview();
    return page;
}

void PreferencesDialog::updateStyleForTheme()
{
    if (!m_themeCombo || !m_styleCombo)
        return;

    const bool win11 = Theme::isWin11Theme(m_themeCombo->currentData().toString());
    const QSignalBlocker blocker(m_styleCombo);
    if (win11) {
        int index = m_styleCombo->findData(QStringLiteral("windows11"));
        if (index < 0)
            index = m_styleCombo->findData(Theme::defaultStyleId());
        if (index >= 0)
            m_styleCombo->setCurrentIndex(index);
        m_styleCombo->setEnabled(false);
        m_styleCombo->setToolTip(
            tr("Windows 11 主题强制使用 Qt 的 windows11 平台样式"));
    } else {
        int index = m_styleCombo->findData(m_preferredStyle);
        if (index < 0)
            index = m_styleCombo->findData(Theme::defaultStyleId());
        if (index >= 0)
            m_styleCombo->setCurrentIndex(index);
        m_styleCombo->setEnabled(true);
        m_styleCombo->setToolTip(tr("Qt 原生控件样式；Windows 11 最接近 WinUI"));
    }
}

void PreferencesDialog::updateThemePreview()
{
    if (!m_themeCombo || !m_themePreview)
        return;

    const QString requested = m_themeCombo->currentData().toString();
    const QString id = Theme::resolveThemeId(requested);
    const Theme::Colors colors = Theme::colors(id);
    QString description;
    if (Theme::isAutoTheme(requested)) {
        description = tr("跟随 Windows 深色/浅色设置；当前使用 %1").arg(Theme::displayName(id));
    } else if (Theme::normalizeId(requested) == QLatin1String("win11light")) {
        description = tr("Qt 6.12 原生 windows11 平台样式（qmodernwindowsstyle），"
                         "固定使用 Windows 11 浅色配色");
    } else if (Theme::isWin11Theme(requested)) {
        description = tr("Qt 6.12 原生 windows11 平台样式（qmodernwindowsstyle）；"
                         "控件按系统浅色/深色绘制");
    } else {
        for (const Theme::Info &info : Theme::availableThemes()) {
            if (info.id == id) {
                description = info.description;
                break;
            }
        }
    }

    m_themePreview->setStyleSheet(
        QStringLiteral("QFrame#ThemePreview { background-color: %1; border: 1px solid %2; "
                       "border-radius: 8px; }")
            .arg(colors.window.name(), colors.border.name()));
    if (m_themeName) {
        QString name;
        if (Theme::isAutoTheme(requested))
            name = tr("跟随系统 · %1").arg(Theme::displayName(id));
        else if (Theme::normalizeId(requested) == QLatin1String("win11light"))
            name = tr("Windows 11 浅色（原生样式）");
        else if (Theme::isWin11Theme(requested))
            name = tr("Windows 11（原生样式）");
        else
            name = Theme::displayName(id);
        m_themeName->setText(name);
        m_themeName->setStyleSheet(
            QStringLiteral("font-size: 18px; font-weight: 600; color: %1;")
                .arg(colors.text.name()));
    }
    if (m_themeDescription) {
        m_themeDescription->setText(description);
        m_themeDescription->setStyleSheet(
            QStringLiteral("color: %1;").arg(colors.textMuted.name()));
    }

    const QVector<QColor> swatches = {colors.window, colors.panel, colors.base,
                                      colors.accent, colors.text};
    for (int index = 0; index < m_themeSwatches.size() && index < swatches.size(); ++index) {
        m_themeSwatches.at(index)->setStyleSheet(
            QStringLiteral("QFrame { background-color: %1; border: 1px solid %2; "
                           "border-radius: 4px; }")
                .arg(swatches.at(index).name(), colors.border.name()));
    }
}

QWidget *PreferencesDialog::createCachePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(9);

    auto *title = new QLabel(tr("环境缓存"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    layout->addWidget(title);

    auto *pathTitle = new QLabel(tr("缓存目录"));
    pathTitle->setObjectName(QStringLiteral("SectionTitle"));
    layout->addWidget(pathTitle);

    m_cachePathLabel = new QLabel;
    m_cachePathLabel->setObjectName(QStringLiteral("DocumentPath"));
    m_cachePathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_cachePathLabel->setWordWrap(true);
    layout->addWidget(m_cachePathLabel);

    auto *pathRow = new QHBoxLayout;
    pathRow->setSpacing(8);
    auto *openButton = new QPushButton(tr("打开缓存目录"));
    openButton->setObjectName(QStringLiteral("GhostButton"));
    auto *clearButton = new QPushButton(tr("清除全部环境缓存"));
    clearButton->setObjectName(QStringLiteral("DangerButton"));
    auto *clearProjectsButton = new QPushButton(tr("清除项目关联缓存"));
    clearProjectsButton->setObjectName(QStringLiteral("GhostButton"));
    pathRow->addWidget(openButton);
    pathRow->addWidget(clearButton);
    pathRow->addWidget(clearProjectsButton);
    pathRow->addStretch();
    layout->addLayout(pathRow);

    m_cacheUsageLabel = new QLabel;
    m_cacheUsageLabel->setObjectName(QStringLiteral("Muted"));
    layout->addWidget(m_cacheUsageLabel);

    layout->addWidget(makeDivider());

    auto *detail = new QLabel(
        tr("缓存内容：根目录、实例（MC 版本 / 加载器 / 客户端 JAR）、mod 列表、"
           "资源包列表、模组元数据（mods.toml / fabric.mod.json / mcmod.info）"
           "以及命名空间解析结果；jar 资源索引在后续阶段接入。\n\n"
           "项目级 reference.json / association.json 位于 %1 下的项目目录，"
           "用于记住参考实例和模型关联解析结果，同样不会写入资源包 ZIP。\n\n"
           "安全约定：缓存只写入 %2，不会写入任何资源包 ZIP；"
           "清除缓存不会删除项目或修改 .minecraft 目录。")
            .arg(QDir::toNativeSeparators(AppPaths::projectsRoot()),
                 QDir::toNativeSeparators(AppPaths::dataRoot())));
    detail->setObjectName(QStringLiteral("PanelHint"));
    detail->setWordWrap(true);
    layout->addWidget(detail);
    layout->addStretch();

    connect(openButton, &QPushButton::clicked, this, [] {
        QDir().mkpath(AppPaths::gameRoot());
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::gameRoot()));
    });
    connect(clearButton, &QPushButton::clicked, this,
            &PreferencesDialog::clearEnvironmentCache);
    connect(clearProjectsButton, &QPushButton::clicked, this,
            &PreferencesDialog::clearProjectAssociationCaches);
    return page;
}

GameSettings PreferencesDialog::settings() const
{
    GameSettings result = m_gamePage ? m_gamePage->settings() : GameSettings{};
    if (m_fallbackModelsCheck)
        result.fallbackModels = m_fallbackModelsCheck->isChecked();
    if (m_fallbackLangCheck)
        result.fallbackLang = m_fallbackLangCheck->isChecked();
    if (m_showReferenceCheck)
        result.showReferenceTextures = m_showReferenceCheck->isChecked();
    if (m_themeCombo)
        result.theme = m_themeCombo->currentData().toString();
    if (m_styleCombo) {
        result.widgetStyle = Theme::isWin11Theme(result.theme)
                                 ? m_preferredStyle
                                 : m_styleCombo->currentData().toString();
    }
    return result;
}

QWidget *PreferencesDialog::createReferencePage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(9);

    auto *title = new QLabel(tr("资源与回退"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    layout->addWidget(title);

    m_fallbackModelsCheck = new QCheckBox(
        tr("资源包缺少 blockstate / model 时，使用参考实例补全结构"));
    m_fallbackModelsCheck->setChecked(true);
    m_fallbackModelsCheck->setToolTip(
        tr("只读取参考实例的 blockstates / models；参考材质不会显示，也不会进入导出。"));
    layout->addWidget(m_fallbackModelsCheck);

    m_fallbackLangCheck =
        new QCheckBox(tr("使用参考实例的 lang 文件补全方块/物品名称"));
    m_fallbackLangCheck->setChecked(true);
    m_fallbackLangCheck->setToolTip(
        tr("资源包缺少语言键时，依次使用参考 mod jar、客户端 jar 的 lang。"));
    layout->addWidget(m_fallbackLangCheck);

    m_showReferenceCheck = new QCheckBox(
        tr("在材质墙显示参考实例中资源包未覆盖的材质"));
    m_showReferenceCheck->setChecked(false);
    m_showReferenceCheck->setToolTip(
        tr("显示为橙色框；资源包内已有的材质显示为蓝色框。参考材质不会自动进入导出。"));
    layout->addWidget(m_showReferenceCheck);

    layout->addWidget(makeDivider());

    auto *note = new QLabel(
        tr("参考实例是只读图层：pack/ 覆盖层 → source/ → 参考 mod jar → 客户端 jar → "
           "assets/indexes + objects。\n\n"
           "只替换材质的资源包开启回退后，可以用参考实例的 blockstates / models / lang "
           "关联到方块与物品；卡片墙、集合模式和导出仍然只包含资源包自身的内容。\n\n"
           "开启“显示参考材质”后，材质墙会额外列出参考 jar 中未被资源包覆盖的材质；"
           "右键可以单个或按命名空间导入到 pack/。\n\n"
           "关闭回退后，资源包缺少结构时只会显示未关联诊断。"));
    note->setObjectName(QStringLiteral("PanelHint"));
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addStretch();
    return page;
}

void PreferencesDialog::setCurrentTab(int index)
{
    if (m_tabs && index >= 0 && index < m_tabs->count())
        m_tabs->setCurrentIndex(index);
}

void PreferencesDialog::save()
{
    if (!m_gamePage)
        return;
    const GameSettings current = settings();
    saveGameSettings(current);
    if (qApp)
        Theme::apply(*qApp, current.theme, current.widgetStyle);
}

void PreferencesDialog::refreshCacheSummary()
{
    if (!m_cachePathLabel || !m_cacheUsageLabel)
        return;

    const QString cacheDir = QDir::toNativeSeparators(AppPaths::gameRoot());
    m_cachePathLabel->setText(cacheDir);

    const qint64 bytes = GameEnvironment::cacheSizeBytes();
    const int files = GameEnvironment::cacheFileCount();
    m_cacheUsageLabel->setText(
        tr("当前占用：%1 · %2 个文件 · 扫描日志：%3")
            .arg(formatBytes(bytes))
            .arg(files)
            .arg(QDir::toNativeSeparators(AppPaths::gameScansRoot())));
}

void PreferencesDialog::clearEnvironmentCache()
{
    const auto answer = QMessageBox::question(
        this, tr("清除环境缓存"),
        tr("将删除 %1 下的全部环境缓存。\n"
           "不会修改 .minecraft 目录，也不会影响已导入的资源包项目。\n\n继续？")
            .arg(QDir::toNativeSeparators(AppPaths::gameRoot())),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    GameEnvironment::clearAllCache();
    refreshCacheSummary();
}

void PreferencesDialog::clearProjectAssociationCaches()
{
    const auto answer = QMessageBox::question(
        this, tr("清除项目关联缓存"),
        tr("将删除所有项目目录下的 association.json（模型关联解析结果）。\n"
           "不会删除项目、资源包或 reference.json，也不会修改 .minecraft。\n\n继续？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    int removed = 0;
    const QDir projects(AppPaths::projectsRoot());
    const QFileInfoList entries =
        projects.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString path =
            QDir(entry.absoluteFilePath()).filePath(QStringLiteral("association.json"));
        if (QFile::exists(path) && QFile::remove(path))
            ++removed;
    }
    refreshCacheSummary();
    QMessageBox::information(this, tr("项目关联缓存"),
                             tr("已删除 %1 个项目的关联缓存。").arg(removed));
}

} // namespace mcstudio
