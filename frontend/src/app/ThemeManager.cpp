#include "app/ThemeManager.h"

#include <QApplication>

#include "app/Easing.h"

namespace mcsm {
namespace {

Palette buildPalette(bool dark)
{
    Palette palette;
    palette.dark = dark;
    if (dark) {
        // Neutral dark: the background carries no colour tint at all, only the
        // accents are pastel (otherwise a dark UI looks noisy).
        palette.windowTop = QColor(0x1C, 0x1C, 0x1E);
        palette.windowBottom = QColor(0x16, 0x16, 0x18);
        palette.surface = QColor(0x24, 0x24, 0x27);
        palette.surfaceAlt = QColor(0x2B, 0x2B, 0x2F);
        palette.surfaceHover = QColor(0x34, 0x34, 0x39);
        palette.border = QColor(0x3A, 0x3A, 0x40);
        palette.borderStrong = QColor(0x4C, 0x4C, 0x54);
        palette.text = QColor(0xEF, 0xEF, 0xF2);
        palette.textMuted = QColor(0xA0, 0xA0, 0xA8);
        palette.pink = QColor(0xF0, 0xAE, 0xCB);
        palette.pinkSoft = QColor(0x5A, 0x3F, 0x4E);
        palette.blue = QColor(0x9F, 0xC3, 0xEC);
        palette.blueSoft = QColor(0x35, 0x43, 0x5C);
        palette.success = QColor(0x8F, 0xCF, 0xB0);
        palette.warning = QColor(0xE8, 0xCF, 0x9C);
        palette.danger = QColor(0xE8, 0xA0, 0xAF);
        palette.shadow = QColor(0, 0, 0, 120);
    } else {
        // Pastel macaron on white: soft mint / butter / lavender / powder blue
        palette.windowTop = QColor(0xFF, 0xFA, 0xFC);
        palette.windowBottom = QColor(0xF4, 0xF8, 0xFD);
        palette.surface = QColor(0xFF, 0xFF, 0xFF);
        palette.surfaceAlt = QColor(0xFB, 0xF8, 0xFD);
        palette.surfaceHover = QColor(0xF6, 0xF1, 0xFA);
        palette.border = QColor(0xEF, 0xE8, 0xF3);
        palette.borderStrong = QColor(0xDE, 0xD3, 0xE8);
        palette.text = QColor(0x3C, 0x37, 0x45);
        palette.textMuted = QColor(0x8E, 0x87, 0x9C);
        palette.pink = QColor(0xF6, 0xB9, 0xD3);
        palette.pinkSoft = QColor(0xFD, 0xE7, 0xF1);
        palette.blue = QColor(0xA9, 0xC6, 0xEA);
        palette.blueSoft = QColor(0xE4, 0xEE, 0xFA);
        palette.success = QColor(0xA3, 0xD9, 0xBE);
        palette.warning = QColor(0xF3, 0xD9, 0xA4);
        palette.danger = QColor(0xEF, 0xA3, 0xB2);
        palette.shadow = QColor(0xB6, 0xA9, 0xC4, 60);
    }
    return palette;
}

} // namespace

ThemeManager *ThemeManager::instance()
{
    static ThemeManager *manager = new ThemeManager();
    return manager;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
    , m_palette(buildPalette(false))
{
}

QString ThemeManager::color(const QColor &value)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(value.red())
        .arg(value.green())
        .arg(value.blue())
        .arg(QString::number(value.alphaF(), 'f', 3));
}

void ThemeManager::reloadPalette()
{
    m_palette = buildPalette(m_dark);
}

void ThemeManager::setDark(bool dark, bool animate)
{
    Q_UNUSED(animate);
    if (m_dark == dark)
        return;
    m_dark = dark;
    reloadPalette();
    emit themeChanged(m_dark);
}

void ThemeManager::toggle()
{
    setDark(!m_dark);
}

QString ThemeManager::accentStyleName() const
{
    return m_accentStyle == AccentStyle::Flat ? QStringLiteral("flat") : QStringLiteral("gradient");
}

void ThemeManager::setAccentStyle(AccentStyle style)
{
    if (m_accentStyle == style)
        return;
    m_accentStyle = style;
    emit themeChanged(m_dark);
}

QStringList ThemeManager::fontFamilies() const
{
    QStringList families;
    if (!m_fontFamily.trimmed().isEmpty())
        families << m_fontFamily.trimmed();
    const QStringList fallbacks = {QStringLiteral("HarmonyOS Sans SC"), QStringLiteral("MiSans"),
                                   QStringLiteral("Noto Sans SC"), QStringLiteral("Source Han Sans SC"),
                                   QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI")};
    for (const QString &fallback : fallbacks) {
        if (!families.contains(fallback))
            families << fallback;
    }
    return families;
}

void ThemeManager::setFontFamily(const QString &family)
{
    if (m_fontFamily == family)
        return;
    m_fontFamily = family.trimmed();
    emit themeChanged(m_dark);
}

void ThemeManager::setAnimationScale(double scale)
{
    m_animationScale = qBound(0.25, scale, 3.0);
    Easing::setSpeedFactor(m_animationScale);
    emit animationSettingsChanged();
}

void ThemeManager::setAnimationsEnabled(bool enabled)
{
    if (m_animationsEnabled == enabled)
        return;
    m_animationsEnabled = enabled;
    emit animationSettingsChanged();
}

QLinearGradient ThemeManager::windowGradient(const Palette &palette, const QRectF &rect)
{
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    if (instance()->accentStyle() == AccentStyle::Flat) {
        // same colours, no gradient: a single calm tone
        gradient.setColorAt(0.0, palette.windowTop);
        gradient.setColorAt(1.0, palette.windowTop);
        return gradient;
    }
    gradient.setColorAt(0.0, palette.windowTop);
    gradient.setColorAt(0.55, palette.dark ? QColor(0x19, 0x19, 0x1B) : QColor(0xFA, 0xF6, 0xFC));
    gradient.setColorAt(1.0, palette.windowBottom);
    return gradient;
}

QLinearGradient ThemeManager::accentGradient(const Palette &palette, const QRectF &rect)
{
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    if (instance()->accentStyle() == AccentStyle::Flat) {
        gradient.setColorAt(0.0, palette.pink);
        gradient.setColorAt(1.0, palette.pink);
        return gradient;
    }
    gradient.setColorAt(0.0, palette.pink);
    gradient.setColorAt(0.5, palette.dark ? QColor(0xC6, 0xB2, 0xDE) : QColor(0xDC, 0xC8, 0xEE));
    gradient.setColorAt(1.0, palette.blue);
    return gradient;
}

QLinearGradient ThemeManager::softAccentGradient(const Palette &palette, const QRectF &rect)
{
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    QColor pink = palette.pink;
    QColor blue = palette.blue;
    pink.setAlphaF(palette.dark ? 0.30 : 0.22);
    blue.setAlphaF(palette.dark ? 0.26 : 0.20);
    if (instance()->accentStyle() == AccentStyle::Flat) {
        gradient.setColorAt(0.0, pink);
        gradient.setColorAt(1.0, pink);
        return gradient;
    }
    gradient.setColorAt(0.0, pink);
    gradient.setColorAt(1.0, blue);
    return gradient;
}

bool ThemeManager::isFlat()
{
    return instance()->accentStyle() == AccentStyle::Flat;
}

QPair<QColor, QColor> ThemeManager::accentColors(const Palette &palette, int firstAlpha, int secondAlpha)
{
    QColor first = palette.pink;
    first.setAlpha(firstAlpha);
    if (isFlat())
        return qMakePair(first, first);
    QColor second = palette.blue;
    second.setAlpha(secondAlpha);
    return qMakePair(first, second);
}

QString ThemeManager::styleSheet() const
{
    const Palette &p = m_palette;
    // Flat style keeps the same colours but drops every gradient, including the
    // ones described inside the style sheet.
    const QString accent =
        m_accentStyle == AccentStyle::Flat
            ? p.pink.name()
            : QStringLiteral("qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 %1, stop:1 %2)")
                  .arg(p.pink.name(), p.blue.name());

    QString qss = QStringLiteral(R"QSS(
* { outline: none; }
QWidget {
    color: %TEXT%;
    font-family: %FONTS%;
    font-size: 13px;
}
QToolTip {
    background: %SURFACE_ALT%;
    color: %TEXT%;
    border: 1px solid %BORDER%;
    border-radius: 8px;
    padding: 6px 8px;
}
QLabel[role="title"] { font-size: 20px; font-weight: 700; }
QLabel[role="subtitle"] { font-size: 15px; font-weight: 600; }
QLabel[role="caption"] { color: %MUTED%; font-size: 12px; }
QLabel[role="hint"] { color: %MUTED%; font-size: 12px; }
QLabel[role="value"] { font-size: 24px; font-weight: 700; }
QLabel[role="chip"] {
    background: %SURFACE_ALT%;
    border: 1px solid %BORDER%;
    border-radius: 9px;
    padding: 2px 9px;
    color: %MUTED%;
    font-size: 12px;
}

QFrame[card="true"] {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: 18px;
}
QFrame[card="alt"] {
    background: %SURFACE_ALT%;
    border: 1px solid %BORDER%;
    border-radius: 16px;
}
QFrame[card="gradient"] {
    border: 1px solid %BORDER%;
    border-radius: 20px;
}
QFrame[role="divider"] { background: %BORDER%; max-height: 1px; border: none; }

QPushButton {
    background: %SURFACE_ALT%;
    border: 1px solid %BORDER%;
    border-radius: 12px;
    padding: 9px 16px;
    color: %TEXT%;
}
QPushButton:hover { background: %SURFACE_HOVER%; border-color: %BORDER_STRONG%; }
QPushButton:pressed { background: %BORDER%; }
QPushButton:disabled { color: %MUTED%; background: %SURFACE_ALT%; border-color: %BORDER%; }
QPushButton[variant="primary"] {
    background: %ACCENT%;
    border: none;
    color: %ON_ACCENT%;
    font-weight: 600;
}
QPushButton[variant="primary"]:hover { background: %ACCENT%; }
QPushButton[variant="ghost"] { background: transparent; border: 1px solid %BORDER%; }
/* variant selectors win over the generic :hover rule, so hover states have to be
   declared per variant (the "refresh" buttons looked completely inert) */
QPushButton[variant="ghost"]:hover {
    background: %SURFACE_HOVER%;
    border: 1px solid %BORDER_STRONG%;
    color: %TEXT%;
}
QPushButton[variant="ghost"]:pressed { background: %BORDER%; }
QPushButton[variant="tool"]:hover { background: %SURFACE_HOVER%; border: 1px solid %BORDER_STRONG%; }
QPushButton[compact="true"]:hover { background: %SURFACE_HOVER%; border: 1px solid %BORDER_STRONG%; }
QPushButton[variant="danger"] { background: %DANGER_SOFT%; border: 1px solid %DANGER%; color: %DANGER_TEXT%; }
QPushButton[variant="tool"] { padding: 6px 10px; border-radius: 10px; font-size: 12px; }
QPushButton[compact="true"] { padding: 5px 10px; border-radius: 9px; font-size: 12px; }

QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: %SURFACE_ALT%;
    border: 1px solid %BORDER%;
    border-radius: 12px;
    /* keep the text box tall enough for the font: padding 5+5, border 1+1 and a
       20px content box leave room for ascenders and descenders */
    padding: 5px 12px;
    min-height: 20px;
    selection-background-color: %PINK%;
    selection-color: #FFFFFF;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus,
QDoubleSpinBox:focus, QComboBox:focus {
    border: 1px solid %PINK%;
    background: %SURFACE%;
}
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: 12px;
    padding: 4px;
    selection-background-color: %SURFACE_HOVER%;
    selection-color: %TEXT%;
}
/* the popup lives in its own container widget; style it explicitly so it never
   falls back to the (possibly dark) system palette */
QComboBoxPrivateContainer, QComboBoxPrivateContainer > QAbstractItemView {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: 12px;
    color: %TEXT%;
}
QPlainTextEdit[mono="true"] {
    font-family: "Cascadia Mono", "JetBrains Mono", Consolas, monospace;
    font-size: 12px;
}

QScrollArea { background: transparent; border: none; }
/* the viewport is a separate widget and would otherwise paint the palette base
   colour (white) for a frame - that was the white flash when switching pages */
QScrollArea > QWidget > QWidget { background: transparent; }
QWidget#pageBody { background: transparent; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
    background: %BORDER_STRONG%;
    border-radius: 5px;
    min-height: 32px;
    min-width: 32px;
}
QScrollBar::handle:hover { background: %PINK%; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QCheckBox { spacing: 8px; }
QCheckBox::indicator {
    width: 18px; height: 18px;
    border-radius: 6px;
    border: 1px solid %BORDER_STRONG%;
    background: %SURFACE_ALT%;
}
QCheckBox::indicator:checked { background: %ACCENT%; border: none; }

QSlider::groove:horizontal {
    height: 6px; border-radius: 3px; background: %BORDER%;
}
QSlider::sub-page:horizontal { background: %ACCENT%; border-radius: 3px; }
QSlider::handle:horizontal {
    /* 14px content + 2px border = 18px box; groove is 6px, so -6px margin centers it */
    width: 14px; height: 14px; margin: -6px 0; border-radius: 9px;
    background: #FFFFFF; border: 2px solid %PINK%;
}
QSlider::handle:horizontal:hover { border: 2px solid %BLUE%; }
QSlider::handle:horizontal:disabled { border: 2px solid %BORDER%; }

QTabWidget::pane { border: none; }
QTabBar::tab {
    background: transparent;
    color: %MUTED%;
    padding: 8px 16px;
    border-radius: 10px;
    margin-right: 4px;
}
QTabBar::tab:selected { background: %SURFACE_ALT%; color: %TEXT%; font-weight: 600; }

QProgressBar {
    background: %SURFACE_ALT%;
    border: 1px solid %BORDER%;
    border-radius: 8px;
    height: 10px;
    text-align: center;
    color: transparent;
}
QProgressBar::chunk { border-radius: 8px; background: %ACCENT%; }

QTableView, QTreeView, QListView {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: 14px;
    gridline-color: %BORDER%;
    selection-background-color: %SURFACE_HOVER%;
    selection-color: %TEXT%;
}
QHeaderView::section {
    background: %SURFACE_ALT%;
    color: %MUTED%;
    border: none;
    border-bottom: 1px solid %BORDER%;
    padding: 8px;
}
QListWidget::item { padding: 6px; border-radius: 10px; }
QListWidget::item:selected { background: %SURFACE_HOVER%; }

QSplitter::handle { background: transparent; }
QMenu {
    background: %SURFACE%;
    border: 1px solid %BORDER%;
    border-radius: 12px;
    padding: 6px;
}
QMenu::item { padding: 6px 18px; border-radius: 8px; }
QMenu::item:selected { background: %SURFACE_HOVER%; }

QDialog { background: %SURFACE%; }
QStatusBar { background: transparent; color: %MUTED%; }
)QSS");

    qss.replace(QStringLiteral("%TEXT%"), p.text.name());
    qss.replace(QStringLiteral("%MUTED%"), p.textMuted.name());
    qss.replace(QStringLiteral("%SURFACE%"), p.surface.name());
    qss.replace(QStringLiteral("%SURFACE_ALT%"), p.surfaceAlt.name());
    qss.replace(QStringLiteral("%SURFACE_HOVER%"), p.surfaceHover.name());
    qss.replace(QStringLiteral("%BORDER_STRONG%"), p.borderStrong.name());
    qss.replace(QStringLiteral("%BORDER%"), p.border.name());
    qss.replace(QStringLiteral("%PINK%"), p.pink.name());
    qss.replace(QStringLiteral("%BLUE%"), p.blue.name());
    qss.replace(QStringLiteral("%ACCENT%"), accent);
    // Pastel accents need dark text for contrast in both themes.
    qss.replace(QStringLiteral("%ON_ACCENT%"), p.dark ? QStringLiteral("#2A2333") : QStringLiteral("#453A50"));
    qss.replace(QStringLiteral("%DANGER%"), p.danger.name());
    QColor dangerSoft = p.danger;
    dangerSoft.setAlphaF(p.dark ? 0.22 : 0.16);
    qss.replace(QStringLiteral("%DANGER_SOFT%"), color(dangerSoft));
    qss.replace(QStringLiteral("%DANGER_TEXT%"), p.dark ? p.danger.name() : QColor(0xC0, 0x3A, 0x52).name());
    qss.replace(QStringLiteral("%SUCCESS%"), p.success.name());
    qss.replace(QStringLiteral("%WARNING%"), p.warning.name());

    QStringList quotedFamilies;
    for (const QString &family : fontFamilies())
        quotedFamilies << QStringLiteral("\"%1\"").arg(family);
    qss.replace(QStringLiteral("%FONTS%"),
                quotedFamilies.join(QStringLiteral(", ")) + QStringLiteral(", sans-serif"));
    return qss;
}

QPalette ThemeManager::qtPalette() const
{
    const Palette &p = m_palette;
    QPalette palette;
    palette.setColor(QPalette::Window, p.windowTop);
    palette.setColor(QPalette::WindowText, p.text);
    palette.setColor(QPalette::Base, p.surface);
    palette.setColor(QPalette::AlternateBase, p.surfaceAlt);
    palette.setColor(QPalette::Text, p.text);
    palette.setColor(QPalette::Button, p.surfaceAlt);
    palette.setColor(QPalette::ButtonText, p.text);
    palette.setColor(QPalette::BrightText, p.danger);
    palette.setColor(QPalette::Highlight, p.pink);
    palette.setColor(QPalette::HighlightedText, QColor(0x2A, 0x23, 0x33));
    palette.setColor(QPalette::PlaceholderText, p.textMuted);
    palette.setColor(QPalette::ToolTipBase, p.surfaceAlt);
    palette.setColor(QPalette::ToolTipText, p.text);
    palette.setColor(QPalette::Link, p.blue);
    palette.setColor(QPalette::LinkVisited, p.blue);
    palette.setColor(QPalette::Light, p.surfaceAlt);
    palette.setColor(QPalette::Midlight, p.border);
    palette.setColor(QPalette::Mid, p.borderStrong);
    palette.setColor(QPalette::Dark, p.windowBottom);
    palette.setColor(QPalette::Shadow, QColor(0, 0, 0, 90));

    palette.setColor(QPalette::Disabled, QPalette::Text, p.textMuted);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, p.textMuted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, p.textMuted);
    palette.setColor(QPalette::Disabled, QPalette::Base, p.surfaceAlt);
    return palette;
}

} // namespace mcsm
