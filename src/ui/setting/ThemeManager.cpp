#include <QStyle>
#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QColor>
#include <QMap>

#include "include/ui/setting/ThemeManager.hpp"
#include "iostream"

ThemeManager *themeManager = new ThemeManager;

extern QString ReadFileText(const QString &path);

// feiyangqingyun stylesheets (ported from upstream nekoray) only style widgets that
// carry custom properties; the base window color comes from the palette. Their leading
// `QPalette{background:#...}` rule is inert inside a Qt stylesheet, so we apply that base
// color as a real QPalette ourselves. Maps the (lower-case) theme name -> base color.
static const QMap<QString, QColor> feiyangBaseColor = {
    {"flatgray",  QColor("#FFFFFF")},
    {"lightblue", QColor("#EAF7FF")},
    {"blacksoft", QColor("#444444")},
};

void ThemeManager::ApplyTheme(const QString &theme, bool force) {
    if (this->system_style_name.isEmpty()) {
        this->system_style_name = qApp->style()->name();
        this->system_palette = qApp->palette();
    }

    if (this->current_theme == theme && !force) {
        return;
    }

    auto lowerTheme = theme.toLower();

    // Leaving a feiyangqingyun theme: restore the OS palette we snapshotted, since the
    // other themes expect the live/standard palette rather than the custom base color.
    if (feiyangBaseColor.contains(current_theme.toLower()) && !feiyangBaseColor.contains(lowerTheme)) {
        qApp->setPalette(system_palette);
    }

    if (feiyangBaseColor.contains(lowerTheme)) {
        qApp->setPalette(QPalette(feiyangBaseColor.value(lowerTheme)));
        qApp->setStyleSheet(ReadFileText(":/qss/" + lowerTheme + ".css"));
    } else if (lowerTheme == "system") {
        qApp->setStyleSheet("");
        qApp->setStyle(system_style_name);
    } else if (lowerTheme == "qdarkstyle") {
        QFile f(":qdarkstyle/dark/darkstyle.qss");
        f.open(QFile::ReadOnly | QFile::Text);
        QTextStream ts(&f);
        qApp->setStyleSheet(ts.readAll());
    } else {
        qApp->setStyleSheet("");
        qApp->setStyle(theme);
    }

    current_theme = theme;

    emit themeChanged(theme);
}
