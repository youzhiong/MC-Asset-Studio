#include "ui/Theme.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QGuiApplication>
#include <QHash>
#include <QPalette>
#include <QSet>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

namespace Theme {
namespace {

struct Spec {
    QString id;
    QString name;
    QString description;
    bool light = false;
    QColor accent;
    QColor warning;
    QColor danger;
};

const QVector<Spec> &specs()
{
    static const QVector<Spec> data = {
        {QStringLiteral("dark"), QStringLiteral("曜石黑"),
         QStringLiteral("经典深色，绿色强调"), false,
         QColor(QStringLiteral("#4ade80")), QColor(QStringLiteral("#fbbf24")),
         QColor(QStringLiteral("#f87171"))},
        {QStringLiteral("light"), QStringLiteral("纯白"),
         QStringLiteral("明亮浅色，适合白天"), true,
         QColor(QStringLiteral("#16a34a")), QColor(QStringLiteral("#b45309")),
         QColor(QStringLiteral("#dc2626"))},
        {QStringLiteral("ocean"), QStringLiteral("海洋"),
         QStringLiteral("深蓝背景，青蓝强调"), false,
         QColor(QStringLiteral("#38bdf8")), QColor(QStringLiteral("#fbbf24")),
         QColor(QStringLiteral("#fb7185"))},
        {QStringLiteral("forest"), QStringLiteral("森林"),
         QStringLiteral("墨绿背景，翠绿强调"), false,
         QColor(QStringLiteral("#34d399")), QColor(QStringLiteral("#fbbf24")),
         QColor(QStringLiteral("#f87171"))},
        {QStringLiteral("sunset"), QStringLiteral("暖阳"),
         QStringLiteral("暖白背景，橙色强调"), true,
         QColor(QStringLiteral("#ea580c")), QColor(QStringLiteral("#b45309")),
         QColor(QStringLiteral("#dc2626"))},
        {QStringLiteral("violet"), QStringLiteral("紫罗兰"),
         QStringLiteral("深紫背景，紫色强调"), false,
         QColor(QStringLiteral("#a78bfa")), QColor(QStringLiteral("#fbbf24")),
         QColor(QStringLiteral("#fb7185"))},
        {QStringLiteral("bluewhite"), QStringLiteral("蓝白"),
         QStringLiteral("浅蓝白底，蓝色强调"), true,
         QColor(QStringLiteral("#2563eb")), QColor(QStringLiteral("#b45309")),
         QColor(QStringLiteral("#dc2626"))},
        {QStringLiteral("mint"), QStringLiteral("薄荷"),
         QStringLiteral("白底蓝绿强调"), true,
         QColor(QStringLiteral("#0d9488")), QColor(QStringLiteral("#b45309")),
         QColor(QStringLiteral("#dc2626"))},
        {QStringLiteral("sakura"), QStringLiteral("樱花"),
         QStringLiteral("粉白背景，粉色强调"), true,
         QColor(QStringLiteral("#db2777")), QColor(QStringLiteral("#b45309")),
         QColor(QStringLiteral("#dc2626"))},
        {QStringLiteral("lavender"), QStringLiteral("薰衣草"),
         QStringLiteral("浅紫白底，紫色强调"), true,
         QColor(QStringLiteral("#7c3aed")), QColor(QStringLiteral("#b45309")),
         QColor(QStringLiteral("#dc2626"))},
    };
    return data;
}

QString g_requested = QStringLiteral("win11light");
QString g_current = QStringLiteral("win11light");
QString g_style = QStringLiteral("Fusion");

QColor mix(const QColor &a, const QColor &b, double amount)
{
    const double t = qBound(0.0, amount, 1.0);
    return QColor(qRound(a.red() * (1.0 - t) + b.red() * t),
                  qRound(a.green() * (1.0 - t) + b.green() * t),
                  qRound(a.blue() * (1.0 - t) + b.blue() * t));
}

const Spec &specFor(const QString &themeId)
{
    QString normalized = normalizeId(themeId);
    if (normalized == QLatin1String("auto"))
        normalized = resolveThemeId(normalized);
    for (const Spec &spec : specs()) {
        if (spec.id == normalized)
            return spec;
    }
    return specs().first();
}

Colors darkPalette(const Spec &spec)
{
    const QColor accent = spec.accent;
    const double tint = spec.id == QStringLiteral("dark") ? 0.0 : 1.0;
    Colors c;
    c.window = mix(QColor(QStringLiteral("#0e1116")), accent, 0.10 * tint);
    c.base = mix(QColor(QStringLiteral("#0b0f14")), accent, 0.075 * tint);
    c.panel = mix(QColor(QStringLiteral("#11161e")), accent, 0.09 * tint);
    c.panelAlt = mix(QColor(QStringLiteral("#0d1219")), accent, 0.08 * tint);
    c.header = mix(QColor(QStringLiteral("#0f141c")), accent, 0.09 * tint);
    c.canvas = mix(QColor(QStringLiteral("#0d1117")), accent, 0.07 * tint);

    c.text = QColor(QStringLiteral("#e6e9ef"));
    c.textMuted = QColor(QStringLiteral("#8b95a7"));
    c.textFaint = QColor(QStringLiteral("#7f8b9e"));
    c.textDisabled = QColor(QStringLiteral("#5f6b7d"));
    c.textSoft = QColor(QStringLiteral("#cbd5e1"));
    c.textBright = QColor(QStringLiteral("#dbe4f0"));
    c.titleText = QColor(QStringLiteral("#f1f5f9"));

    c.button = mix(QColor(QStringLiteral("#1a2230")), accent, 0.10 * tint);
    c.buttonHover = mix(QColor(QStringLiteral("#202b3a")), accent, 0.12 * tint);
    c.buttonDisabled = mix(QColor(QStringLiteral("#121821")), accent, 0.04 * tint);
    c.border = mix(QColor(QStringLiteral("#1f2836")), accent, 0.10 * tint);
    c.borderStrong = mix(QColor(QStringLiteral("#273244")), accent, 0.12 * tint);
    c.focusBorder = mix(QColor(QStringLiteral("#3f7f5d")), accent, 0.35 * tint);
    c.menuBg = mix(QColor(QStringLiteral("#151b24")), accent, 0.10 * tint);
    c.menuHover = mix(QColor(QStringLiteral("#1e2a3a")), accent, 0.14 * tint);
    c.hover = mix(QColor(QStringLiteral("#172030")), accent, 0.12 * tint);
    c.tooltipBg = mix(QColor(QStringLiteral("#1b2230")), accent, 0.05 * tint);
    c.tooltipBorder = mix(QColor(QStringLiteral("#34445c")), accent, 0.10 * tint);
    c.scroll = mix(QColor(QStringLiteral("#2a3749")), accent, 0.06 * tint);
    c.scrollHover = mix(QColor(QStringLiteral("#3a4b62")), accent, 0.10 * tint);
    c.slider = QColor(QStringLiteral("#94a3b8"));

    c.badgeBg = mix(QColor(QStringLiteral("#243044")), accent, 0.06 * tint);
    c.badgeBorder = mix(QColor(QStringLiteral("#3b4a63")), accent, 0.12 * tint);

    c.accent = accent;
    c.accentStrong = mix(accent, QColor(QStringLiteral("#000000")), 0.55);
    c.accentStrongHover = mix(c.accentStrong, QColor(QStringLiteral("#ffffff")), 0.10);
    c.accentStrongBorder = mix(c.accentStrong, QColor(QStringLiteral("#ffffff")), 0.16);
    c.accentSoft = mix(QColor(QStringLiteral("#11161e")), accent, 0.22);
    c.accentBorder = mix(QColor(QStringLiteral("#273244")), accent, 0.50);
    c.accentText = mix(accent, QColor(QStringLiteral("#ffffff")), 0.62);
    c.onAccent = QColor(QStringLiteral("#f0fdf4"));
    c.selection = mix(accent, QColor(QStringLiteral("#000000")), 0.60);

    c.warning = spec.warning;
    c.danger = spec.danger;
    c.dangerBg = QColor(QStringLiteral("#3a1d22"));
    c.dangerBorder = QColor(QStringLiteral("#7f2d38"));
    c.dangerText = QColor(QStringLiteral("#fecdd3"));
    c.dangerHover = QColor(QStringLiteral("#4a242b"));
    c.reference = QColor(QStringLiteral("#f59e0b"));
    c.pack = QColor(QStringLiteral("#38bdf8"));
    c.success = QColor(QStringLiteral("#86efac"));
    c.checkerA = QColor(QStringLiteral("#101720"));
    c.checkerB = QColor(QStringLiteral("#0c1118"));
    return c;
}

Colors lightPalette(const Spec &spec)
{
    const QColor accent = spec.accent;
    Colors c;
    c.window = mix(QColor(QStringLiteral("#f4f6f9")), accent, 0.03);
    c.base = QColor(QStringLiteral("#ffffff"));
    c.panel = mix(QColor(QStringLiteral("#eef2f6")), accent, 0.03);
    c.panelAlt = mix(QColor(QStringLiteral("#e9eef4")), accent, 0.03);
    c.header = QColor(QStringLiteral("#ffffff"));
    c.canvas = mix(QColor(QStringLiteral("#f7f9fb")), accent, 0.025);

    c.text = QColor(QStringLiteral("#1f2937"));
    c.textMuted = QColor(QStringLiteral("#5b6573"));
    c.textFaint = QColor(QStringLiteral("#6b7280"));
    c.textDisabled = QColor(QStringLiteral("#9aa4b1"));
    c.textSoft = QColor(QStringLiteral("#374151"));
    c.textBright = QColor(QStringLiteral("#111827"));
    c.titleText = QColor(QStringLiteral("#0f172a"));

    c.button = mix(QColor(QStringLiteral("#e8eef5")), accent, 0.04);
    c.buttonHover = mix(QColor(QStringLiteral("#dde7f2")), accent, 0.05);
    c.buttonDisabled = QColor(QStringLiteral("#eef2f6"));
    c.border = QColor(QStringLiteral("#d7dee7"));
    c.borderStrong = QColor(QStringLiteral("#c5cfdb"));
    c.focusBorder = accent;
    c.menuBg = QColor(QStringLiteral("#ffffff"));
    c.menuHover = mix(QColor(QStringLiteral("#eef4fb")), accent, 0.05);
    c.hover = mix(QColor(QStringLiteral("#e8eef7")), accent, 0.05);
    c.tooltipBg = QColor(QStringLiteral("#ffffff"));
    c.tooltipBorder = QColor(QStringLiteral("#c5cfdb"));
    c.scroll = QColor(QStringLiteral("#c5cfdb"));
    c.scrollHover = QColor(QStringLiteral("#aeb9c7"));
    c.slider = QColor(QStringLiteral("#64748b"));

    c.badgeBg = mix(QColor(QStringLiteral("#e8eef5")), accent, 0.05);
    c.badgeBorder = QColor(QStringLiteral("#c5cfdb"));

    c.accent = accent;
    c.accentStrong = mix(accent, QColor(QStringLiteral("#000000")), 0.35);
    c.accentStrongHover = mix(c.accentStrong, QColor(QStringLiteral("#000000")), 0.10);
    c.accentStrongBorder = mix(c.accentStrong, QColor(QStringLiteral("#ffffff")), 0.08);
    c.accentSoft = mix(QColor(QStringLiteral("#ffffff")), accent, 0.14);
    c.accentBorder = mix(QColor(QStringLiteral("#ffffff")), accent, 0.40);
    c.accentText = mix(accent, QColor(QStringLiteral("#000000")), 0.55);
    c.onAccent = QColor(QStringLiteral("#ffffff"));
    c.selection = c.accentStrong;

    c.warning = spec.warning;
    c.danger = spec.danger;
    c.dangerBg = QColor(QStringLiteral("#fdecec"));
    c.dangerBorder = QColor(QStringLiteral("#f3b4b4"));
    c.dangerText = QColor(QStringLiteral("#991b1b"));
    c.dangerHover = QColor(QStringLiteral("#fbdcdc"));
    c.reference = QColor(QStringLiteral("#d97706"));
    c.pack = QColor(QStringLiteral("#0284c7"));
    c.success = QColor(QStringLiteral("#15803d"));
    c.checkerA = QColor(QStringLiteral("#ffffff"));
    c.checkerB = QColor(QStringLiteral("#eef2f6"));
    return c;
}

Colors win11Palette(bool dark)
{
    Colors c = dark ? darkPalette(specFor(QStringLiteral("dark")))
                    : lightPalette(specFor(QStringLiteral("light")));

    if (dark) {
        c.window = QColor(QStringLiteral("#202020"));
        c.base = QColor(QStringLiteral("#272727"));
        c.panel = QColor(QStringLiteral("#2b2b2b"));
        c.panelAlt = QColor(QStringLiteral("#242424"));
        c.header = QColor(QStringLiteral("#202020"));
        c.canvas = QColor(QStringLiteral("#1f1f1f"));
        c.text = QColor(QStringLiteral("#ffffff"));
        c.textMuted = QColor(QStringLiteral("#c5c5c5"));
        c.textFaint = QColor(QStringLiteral("#a8a8a8"));
        c.textDisabled = QColor(QStringLiteral("#7a7a7a"));
        c.textSoft = QColor(QStringLiteral("#e0e0e0"));
        c.textBright = QColor(QStringLiteral("#ffffff"));
        c.titleText = QColor(QStringLiteral("#ffffff"));
        c.button = QColor(QStringLiteral("#2d2d2d"));
        c.buttonHover = QColor(QStringLiteral("#383838"));
        c.buttonDisabled = QColor(QStringLiteral("#262626"));
        c.border = QColor(QStringLiteral("#3a3a3a"));
        c.borderStrong = QColor(QStringLiteral("#4a4a4a"));
        c.focusBorder = QColor(QStringLiteral("#60cdff"));
        c.menuBg = QColor(QStringLiteral("#2b2b2b"));
        c.menuHover = QColor(QStringLiteral("#383838"));
        c.hover = QColor(QStringLiteral("#333333"));
        c.tooltipBg = QColor(QStringLiteral("#2b2b2b"));
        c.tooltipBorder = QColor(QStringLiteral("#4a4a4a"));
        c.scroll = QColor(QStringLiteral("#5a5a5a"));
        c.scrollHover = QColor(QStringLiteral("#7a7a7a"));
        c.slider = QColor(QStringLiteral("#9a9a9a"));
        c.badgeBg = QColor(QStringLiteral("#333333"));
        c.badgeBorder = QColor(QStringLiteral("#4a4a4a"));
        c.accent = QColor(QStringLiteral("#60cdff"));
        c.accentStrong = QColor(QStringLiteral("#0078d4"));
        c.accentStrongHover = QColor(QStringLiteral("#106ebe"));
        c.accentStrongBorder = QColor(QStringLiteral("#2b88d8"));
        c.accentSoft = QColor(QStringLiteral("#1f3a4d"));
        c.accentBorder = QColor(QStringLiteral("#2b5a7a"));
        c.accentText = QColor(QStringLiteral("#9fe0ff"));
        c.onAccent = QColor(QStringLiteral("#ffffff"));
        c.selection = QColor(QStringLiteral("#0078d4"));
        c.warning = QColor(QStringLiteral("#fce100"));
        c.danger = QColor(QStringLiteral("#ff99a4"));
        c.dangerBg = QColor(QStringLiteral("#442726"));
        c.dangerBorder = QColor(QStringLiteral("#6b3a3a"));
        c.dangerText = QColor(QStringLiteral("#ffb3b3"));
        c.dangerHover = QColor(QStringLiteral("#5a2f2f"));
        c.reference = QColor(QStringLiteral("#fce100"));
        c.pack = QColor(QStringLiteral("#60cdff"));
        c.success = QColor(QStringLiteral("#6ccb5f"));
        c.checkerA = QColor(QStringLiteral("#1f1f1f"));
        c.checkerB = QColor(QStringLiteral("#2a2a2a"));
    } else {
        c.window = QColor(QStringLiteral("#f3f3f3"));
        c.base = QColor(QStringLiteral("#ffffff"));
        c.panel = QColor(QStringLiteral("#f9f9f9"));
        c.panelAlt = QColor(QStringLiteral("#f3f3f3"));
        c.header = QColor(QStringLiteral("#f3f3f3"));
        c.canvas = QColor(QStringLiteral("#f9f9f9"));
        c.text = QColor(QStringLiteral("#1a1a1a"));
        c.textMuted = QColor(QStringLiteral("#5c5c5c"));
        c.textFaint = QColor(QStringLiteral("#6b6b6b"));
        c.textDisabled = QColor(QStringLiteral("#9a9a9a"));
        c.textSoft = QColor(QStringLiteral("#333333"));
        c.textBright = QColor(QStringLiteral("#000000"));
        c.titleText = QColor(QStringLiteral("#000000"));
        c.button = QColor(QStringLiteral("#fdfdfd"));
        c.buttonHover = QColor(QStringLiteral("#f5f5f5"));
        c.buttonDisabled = QColor(QStringLiteral("#f0f0f0"));
        c.border = QColor(QStringLiteral("#e5e5e5"));
        c.borderStrong = QColor(QStringLiteral("#d6d6d6"));
        c.focusBorder = QColor(QStringLiteral("#0067c0"));
        c.menuBg = QColor(QStringLiteral("#ffffff"));
        c.menuHover = QColor(QStringLiteral("#f0f0f0"));
        c.hover = QColor(QStringLiteral("#f5f5f5"));
        c.tooltipBg = QColor(QStringLiteral("#ffffff"));
        c.tooltipBorder = QColor(QStringLiteral("#d6d6d6"));
        c.scroll = QColor(QStringLiteral("#c8c8c8"));
        c.scrollHover = QColor(QStringLiteral("#a8a8a8"));
        c.slider = QColor(QStringLiteral("#8a8a8a"));
        c.badgeBg = QColor(QStringLiteral("#f0f0f0"));
        c.badgeBorder = QColor(QStringLiteral("#d6d6d6"));
        c.accent = QColor(QStringLiteral("#0067c0"));
        c.accentStrong = QColor(QStringLiteral("#005a9e"));
        c.accentStrongHover = QColor(QStringLiteral("#0067c0"));
        c.accentStrongBorder = QColor(QStringLiteral("#004f8a"));
        c.accentSoft = QColor(QStringLiteral("#e5f1fb"));
        c.accentBorder = QColor(QStringLiteral("#99c9ef"));
        c.accentText = QColor(QStringLiteral("#004a8a"));
        c.onAccent = QColor(QStringLiteral("#ffffff"));
        c.selection = QColor(QStringLiteral("#0067c0"));
        c.warning = QColor(QStringLiteral("#9d5d00"));
        c.danger = QColor(QStringLiteral("#c42b1c"));
        c.dangerBg = QColor(QStringLiteral("#fde7e9"));
        c.dangerBorder = QColor(QStringLiteral("#f1b8bb"));
        c.dangerText = QColor(QStringLiteral("#a80000"));
        c.dangerHover = QColor(QStringLiteral("#fbd7d9"));
        c.reference = QColor(QStringLiteral("#c47f00"));
        c.pack = QColor(QStringLiteral("#0067c0"));
        c.success = QColor(QStringLiteral("#0f7b0f"));
        c.checkerA = QColor(QStringLiteral("#ffffff"));
        c.checkerB = QColor(QStringLiteral("#f0f0f0"));
    }
    return c;
}

QString win11Stylesheet(const Colors &c)
{
    return QStringLiteral(
               "QWidget { font-family: \"Microsoft YaHei UI\", \"Segoe UI\"; }"
               "QLabel#PanelTitle { font-size: 14px; font-weight: 600; }"
               "QLabel#PanelHint { font-size: 12px; }"
               "QLabel#SectionTitle { font-size: 11px; font-weight: 600; }"
               "QLabel#EmptyTitle { font-size: 23px; font-weight: 600; }"
               "QLabel#EmptyText { font-size: 13px; }"
               "QLabel#BoardTitle { font-size: 15px; font-weight: 600; }"
               "QLabel#BoardHint { font-size: 12px; }"
               "QFrame#Sidebar, QFrame#Inspector { background-color: %1; }"
               "QFrame#CenterPanel { background-color: %2; }"
               "QFrame#ProjectCard { background-color: %3; border: 1px solid %4; "
               "border-radius: 8px; }"
               "QFrame#PreviewFrame { background-color: %5; border: 1px solid %4; "
               "border-radius: 6px; }"
               "QFrame#DocumentHeader, QFrame#TextureBoardHeader, QFrame#CollectionHeader "
               "{ background-color: %6; border-bottom: 1px solid %4; }"
               "QFrame#EditorToolColumn { background-color: %1; border-right: 1px solid %4; }"
               "QFrame#EditorSidePanel { background-color: %1; border-left: 1px solid %4; }"
               "QFrame#EditorTimeline { background-color: %6; border-top: 1px solid %4; }"
               "QFrame#Divider { background-color: %4; }"
               "QFrame#VersionBadge { background-color: %7; border: 1px solid %8; "
               "border-radius: 8px; }"
               "QFrame#VersionBadge QLabel { color: %9; font-size: 10px; font-weight: 600; }"
               "QFrame#Badge { background-color: %10; border: 1px solid %4; "
               "border-radius: 8px; }"
               "QFrame#Badge QLabel { font-size: 11px; }"
               "QLabel#IconPreview, QLabel#CoverPreview { background-color: %5; "
               "border: 1px solid %4; border-radius: 8px; }"
               "QFrame#ThemePreview { background-color: %1; border: 1px solid %4; "
               "border-radius: 8px; }"
               "QScrollArea#EditorSidePanelScroll, QScrollArea#EditorSidePanelScroll > QWidget "
               "> QWidget { background-color: %1; border: none; }")
        .arg(c.panel.name(), c.window.name(), c.panelAlt.name(), c.border.name(),
             c.base.name(), c.header.name(), c.accentSoft.name(), c.accentBorder.name(),
             c.accentText.name(), c.badgeBg.name());
}

QHash<QString, QColor> tokenMap(const Colors &c)
{
    return {
        {QStringLiteral("#0e1116"), c.window},
        {QStringLiteral("#0b0f14"), c.base},
        {QStringLiteral("#11161e"), c.panel},
        {QStringLiteral("#0d1219"), c.panelAlt},
        {QStringLiteral("#0f141c"), c.header},
        {QStringLiteral("#0d1117"), c.canvas},
        {QStringLiteral("#e6e9ef"), c.text},
        {QStringLiteral("#8b95a7"), c.textMuted},
        {QStringLiteral("#7f8b9e"), c.textFaint},
        {QStringLiteral("#5f6b7d"), c.textDisabled},
        {QStringLiteral("#cbd5e1"), c.textSoft},
        {QStringLiteral("#dbe4f0"), c.textBright},
        {QStringLiteral("#f1f5f9"), c.titleText},
        {QStringLiteral("#1a2230"), c.button},
        {QStringLiteral("#202b3a"), c.buttonHover},
        {QStringLiteral("#121821"), c.buttonDisabled},
        {QStringLiteral("#1f2836"), c.border},
        {QStringLiteral("#273244"), c.borderStrong},
        {QStringLiteral("#3f7f5d"), c.focusBorder},
        {QStringLiteral("#151b24"), c.menuBg},
        {QStringLiteral("#1e2a3a"), c.menuHover},
        {QStringLiteral("#172030"), c.hover},
        {QStringLiteral("#1b2230"), c.tooltipBg},
        {QStringLiteral("#34445c"), c.tooltipBorder},
        {QStringLiteral("#2a3749"), c.scroll},
        {QStringLiteral("#3a4b62"), c.scrollHover},
        {QStringLiteral("#94a3b8"), c.slider},
        {QStringLiteral("#243044"), c.badgeBg},
        {QStringLiteral("#3b4a63"), c.badgeBorder},
        {QStringLiteral("#4ade80"), c.accent},
        {QStringLiteral("#2f7d52"), c.accentStrong},
        {QStringLiteral("#3f9a66"), c.accentStrongBorder},
        {QStringLiteral("#37945f"), c.accentStrongHover},
        {QStringLiteral("#1c3328"), c.accentSoft},
        {QStringLiteral("#2f5c43"), c.accentBorder},
        {QStringLiteral("#d1fae5"), c.accentText},
        {QStringLiteral("#f0fdf4"), c.onAccent},
        {QStringLiteral("#256b46"), c.selection},
        {QStringLiteral("#fbbf24"), c.warning},
        {QStringLiteral("#3a1d22"), c.dangerBg},
        {QStringLiteral("#7f2d38"), c.dangerBorder},
        {QStringLiteral("#fecdd3"), c.dangerText},
        {QStringLiteral("#4a242b"), c.dangerHover},
    };
}

} // namespace

QVector<Info> availableThemes()
{
    QVector<Info> result;
    for (const Spec &spec : specs())
        result.append(Info{spec.id, spec.name, spec.description, spec.light});
    return result;
}

QVector<StyleInfo> availableStyles()
{
    const QStringList keys = QStyleFactory::keys();
    const auto keyFor = [&keys](const QString &wanted) -> QString {
        for (const QString &key : keys) {
            if (key.compare(wanted, Qt::CaseInsensitive) == 0)
                return key;
        }
        return {};
    };

    QVector<StyleInfo> result;
    const auto add = [&result, &keyFor](const QString &key, const QString &name,
                                        const QString &description) {
        const QString actual = keyFor(key);
        if (!actual.isEmpty())
            result.append(StyleInfo{actual, name, description});
    };
    add(QStringLiteral("Fusion"), QStringLiteral("Fusion（跨平台）"),
        QStringLiteral("当前设计使用的默认样式，颜色最可控"));
    add(QStringLiteral("windows11"), QStringLiteral("Windows 11（接近 WinUI）"),
        QStringLiteral("原生圆角、现代控件，最接近 WinUI"));
    add(QStringLiteral("windowsvista"), QStringLiteral("Windows Vista/7"),
        QStringLiteral("Windows 原生经典控件"));
    add(QStringLiteral("Windows"), QStringLiteral("Windows 经典"),
        QStringLiteral("旧版 Windows 控件"));

    QSet<QString> seen;
    for (const StyleInfo &info : result)
        seen.insert(info.id.toLower());
    for (const QString &key : keys) {
        if (seen.contains(key.toLower()))
            continue;
        result.append(StyleInfo{key, key, QStringLiteral("Qt 提供的其他控件样式")});
    }
    if (result.isEmpty())
        result.append(StyleInfo{QStringLiteral("Fusion"), QStringLiteral("Fusion"),
                                QStringLiteral("跨平台默认样式")});
    return result;
}

QString defaultThemeId()
{
    return QStringLiteral("win11light");
}

QString defaultStyleId()
{
    const QVector<StyleInfo> styles = availableStyles();
    for (const StyleInfo &info : styles) {
        if (info.id.compare(QStringLiteral("Fusion"), Qt::CaseInsensitive) == 0)
            return info.id;
    }
    return styles.isEmpty() ? QStringLiteral("Fusion") : styles.first().id;
}

QString normalizeId(const QString &themeId)
{
    if (themeId.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("auto");
    if (themeId.compare(QStringLiteral("win11"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("win11");
    if (themeId.compare(QStringLiteral("win11light"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("win11light");
    for (const Spec &spec : specs()) {
        if (spec.id.compare(themeId, Qt::CaseInsensitive) == 0)
            return spec.id;
    }
    return defaultThemeId();
}

QString normalizeStyleId(const QString &styleName)
{
    for (const StyleInfo &info : availableStyles()) {
        if (info.id.compare(styleName, Qt::CaseInsensitive) == 0)
            return info.id;
    }
    return defaultStyleId();
}

QString displayName(const QString &themeId)
{
    const QString normalized = normalizeId(themeId);
    if (normalized == QLatin1String("auto"))
        return QStringLiteral("跟随系统");
    if (normalized == QLatin1String("win11"))
        return QStringLiteral("Windows 11");
    if (normalized == QLatin1String("win11light"))
        return QStringLiteral("Windows 11 浅色");
    return specFor(themeId).name;
}

QString styleDisplayName(const QString &styleName)
{
    const QString normalized = normalizeStyleId(styleName);
    for (const StyleInfo &info : availableStyles()) {
        if (info.id == normalized)
            return info.name;
    }
    return normalized;
}

bool isLight(const QString &themeId)
{
    const QString normalized = normalizeId(themeId);
    if (normalized == QLatin1String("win11light"))
        return true;
    if (normalized == QLatin1String("win11"))
        return !systemPrefersDark();
    return specFor(themeId).light;
}

bool isAutoTheme(const QString &themeId)
{
    return normalizeId(themeId) == QLatin1String("auto");
}

bool isWin11Theme(const QString &themeId)
{
    const QString normalized = normalizeId(themeId);
    return normalized == QLatin1String("win11")
        || normalized == QLatin1String("win11light");
}

bool systemPrefersDark()
{
    const QStyleHints *hints = QGuiApplication::styleHints();
    if (!hints)
        return true;
    return hints->colorScheme() != Qt::ColorScheme::Light;
}

QString resolveThemeId(const QString &themeId)
{
    const QString normalized = normalizeId(themeId);
    if (normalized == QLatin1String("auto"))
        return systemPrefersDark() ? QStringLiteral("dark") : QStringLiteral("light");
    return normalized;
}

Colors colors(const QString &themeId)
{
    const QString normalized = normalizeId(themeId.isEmpty() ? g_current : themeId);
    if (normalized == QLatin1String("win11light"))
        return win11Palette(false);
    if (normalized == QLatin1String("win11"))
        return win11Palette(systemPrefersDark());
    const Spec &spec = specFor(normalized);
    return spec.light ? lightPalette(spec) : darkPalette(spec);
}

QString currentThemeId()
{
    return g_current;
}

QString requestedThemeId()
{
    return g_requested;
}

QString currentStyleName()
{
    return g_style;
}

void apply(QApplication &app, const QString &themeId, const QString &styleName)
{
    const QString requested = normalizeId(themeId.isEmpty() ? g_requested : themeId);
    g_requested = requested;
    g_current = resolveThemeId(requested);

    const bool win11 = isWin11Theme(g_current);
    const QString styleId = win11
                                ? QStringLiteral("windows11")
                                : normalizeStyleId(styleName.isEmpty() ? g_style : styleName);
    g_style = styleId;

    const bool win11Light = g_current == QLatin1String("win11light");
    const Colors c = win11
                         ? win11Palette(win11Light ? false : systemPrefersDark())
                         : colors(g_current);

    QStyle *widgetStyle = QStyleFactory::create(styleId);
    if (!widgetStyle)
        widgetStyle = QStyleFactory::create(QStringLiteral("Fusion"));
    if (widgetStyle)
        app.setStyle(widgetStyle);

    QPalette palette;
    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.base);
    palette.setColor(QPalette::AlternateBase, c.panelAlt);
    palette.setColor(QPalette::ToolTipBase, c.tooltipBg);
    palette.setColor(QPalette::ToolTipText, c.text);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::Button, c.button);
    palette.setColor(QPalette::ButtonText, c.text);
    palette.setColor(QPalette::BrightText, c.onAccent);
    palette.setColor(QPalette::Link, c.accent);
    palette.setColor(QPalette::LinkVisited, c.accentStrong);
    palette.setColor(QPalette::Highlight, c.selection);
    palette.setColor(QPalette::HighlightedText, c.onAccent);
    palette.setColor(QPalette::PlaceholderText, c.textDisabled);
    palette.setColor(QPalette::Light, c.panel);
    palette.setColor(QPalette::Midlight, c.border);
    palette.setColor(QPalette::Mid, c.borderStrong);
    palette.setColor(QPalette::Dark, c.borderStrong);
    palette.setColor(QPalette::Shadow, c.textDisabled);

    palette.setColor(QPalette::Disabled, QPalette::Text, c.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, c.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, c.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, c.button);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, c.textDisabled);
    app.setPalette(palette);

    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPointSizeF(9.5);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);

    QFile stylesheet(QStringLiteral(":/theme.qss"));
    if (win11) {
        app.setStyleSheet(win11Stylesheet(c));
    } else if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString style = QString::fromUtf8(stylesheet.readAll());
        const QHash<QString, QColor> replacements = tokenMap(c);
        for (auto it = replacements.constBegin(); it != replacements.constEnd(); ++it)
            style.replace(it.key(), it.value().name(QColor::HexRgb), Qt::CaseInsensitive);
        app.setStyleSheet(style);
    }

    for (QWidget *widget : QApplication::allWidgets())
        widget->update();
}

void installSystemThemeWatcher()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;

    QStyleHints *hints = QGuiApplication::styleHints();
    if (!hints)
        return;
    QObject::connect(hints, &QStyleHints::colorSchemeChanged, hints, [](Qt::ColorScheme) {
        if (g_requested != QLatin1String("auto")
            && g_current != QLatin1String("win11")) {
            return;
        }
        auto *application = qobject_cast<QApplication *>(QCoreApplication::instance());
        if (application)
            apply(*application, g_requested, g_style);
    });
}

} // namespace Theme
