#pragma once

#include <QColor>
#include <QString>
#include <QVector>

class QApplication;

namespace Theme {

struct Colors {
    QColor window;
    QColor base;
    QColor panel;
    QColor panelAlt;
    QColor header;
    QColor canvas;
    QColor text;
    QColor textMuted;
    QColor textFaint;
    QColor textDisabled;
    QColor textSoft;
    QColor textBright;
    QColor titleText;
    QColor button;
    QColor buttonHover;
    QColor buttonDisabled;
    QColor border;
    QColor borderStrong;
    QColor focusBorder;
    QColor menuBg;
    QColor menuHover;
    QColor hover;
    QColor tooltipBg;
    QColor tooltipBorder;
    QColor scroll;
    QColor scrollHover;
    QColor slider;
    QColor badgeBg;
    QColor badgeBorder;
    QColor accent;
    QColor accentStrong;
    QColor accentStrongBorder;
    QColor accentStrongHover;
    QColor accentSoft;
    QColor accentBorder;
    QColor accentText;
    QColor onAccent;
    QColor selection;
    QColor warning;
    QColor danger;
    QColor dangerBg;
    QColor dangerBorder;
    QColor dangerText;
    QColor dangerHover;
    QColor reference;
    QColor pack;
    QColor success;
    QColor checkerA;
    QColor checkerB;
};

struct Info {
    QString id;
    QString name;
    QString description;
    bool light = false;
};

struct StyleInfo {
    QString id;
    QString name;
    QString description;
};

QVector<Info> availableThemes();
QVector<StyleInfo> availableStyles();
QString defaultThemeId();
QString defaultStyleId();
QString normalizeId(const QString &themeId);
QString normalizeStyleId(const QString &styleName);
QString displayName(const QString &themeId);
QString styleDisplayName(const QString &styleName);
bool isLight(const QString &themeId);
bool isAutoTheme(const QString &themeId);
bool isWin11Theme(const QString &themeId);
bool systemPrefersDark();
QString resolveThemeId(const QString &themeId);

Colors colors(const QString &themeId = QString());
QString currentThemeId();
QString requestedThemeId();
QString currentStyleName();

// Applies palette, font, widget style and the tokenized stylesheet. Passing an
// empty id keeps the current theme/style. "auto" follows the system color scheme.
void apply(QApplication &app, const QString &themeId = QString(),
           const QString &styleName = QString());

// Re-applies the auto theme when Windows switches between light and dark.
void installSystemThemeWatcher();

} // namespace Theme
