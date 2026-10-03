#pragma once

#include <QColor>
#include <QLinearGradient>
#include <QObject>
#include <QPalette>
#include <QString>

namespace mcsm {

/// Colour tokens for one theme. The accent palette stays constant (macaron
/// pink to blue) while surfaces flip between dark and light.
struct Palette
{
    bool dark = false;
    QColor windowTop;
    QColor windowBottom;
    QColor surface;
    QColor surfaceAlt;
    QColor surfaceHover;
    QColor border;
    QColor borderStrong;
    QColor text;
    QColor textMuted;
    QColor pink;
    QColor pinkSoft;
    QColor blue;
    QColor blueSoft;
    QColor success;
    QColor warning;
    QColor danger;
    QColor shadow;
};

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    /// Gradient = macaron pink→blue blends, Flat = the same colours without any
    /// gradient (simpler, flatter look).
    enum class AccentStyle { Gradient, Flat };

    static ThemeManager *instance();

    bool isDark() const { return m_dark; }
    void setDark(bool dark, bool animate = true);
    void toggle();

    const Palette &palette() const { return m_palette; }
    QString name() const { return m_dark ? QStringLiteral("dark") : QStringLiteral("light"); }

    AccentStyle accentStyle() const { return m_accentStyle; }
    void setAccentStyle(AccentStyle style);
    QString accentStyleName() const;

    /// Preferred UI font family (empty = built-in fallback stack).
    QString fontFamily() const { return m_fontFamily; }
    void setFontFamily(const QString &family);
    /// Font stack used by the app and by the generated style sheet.
    QStringList fontFamilies() const;

    double animationScale() const { return m_animationScale; }
    void setAnimationScale(double scale);
    bool animationsEnabled() const { return m_animationsEnabled; }
    void setAnimationsEnabled(bool enabled);

    /// Full application style sheet for the active palette.
    QString styleSheet() const;
    /// Qt palette matching the active theme. Style sheets do not reach every
    /// part of a widget (combo box popups, menus, tooltips), so the palette has
    /// to be set as well - otherwise a dark Windows theme leaks into the light
    /// app theme and popups render black on black.
    QPalette qtPalette() const;

    static QLinearGradient windowGradient(const Palette &palette, const QRectF &rect);
    static QLinearGradient accentGradient(const Palette &palette, const QRectF &rect);
    static QLinearGradient softAccentGradient(const Palette &palette, const QRectF &rect);

    /// True when the user chose the flat (no gradient) colour style.
    static bool isFlat();
    /// The two accent colours used for pink→blue blends. In flat mode both
    /// entries are the same colour, so callers can keep writing two stops.
    static QPair<QColor, QColor> accentColors(const Palette &palette,
                                              int firstAlpha = 255,
                                              int secondAlpha = 255);

signals:
    void themeChanged(bool dark);
    void animationSettingsChanged();

private:
    explicit ThemeManager(QObject *parent = nullptr);
    void reloadPalette();
    static QString color(const QColor &value);

    bool m_dark = false;   // macaron light theme is the default look
    AccentStyle m_accentStyle = AccentStyle::Gradient;
    QString m_fontFamily;
    double m_animationScale = 1.0;
    bool m_animationsEnabled = true;
    Palette m_palette;
};

} // namespace mcsm
