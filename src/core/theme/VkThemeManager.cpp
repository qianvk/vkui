// SPDX-License-Identifier: MIT

#include "private/VkThemeManager_p.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QOperatingSystemVersion>
#include <QtCore/QString>
#include <QtGui/QFontDatabase>
#include <QtGui/QPalette>
#include <algorithm>
#include <vkui/core/VkThemeManager.h>

namespace vkui {
namespace {

QGuiApplication* currentGuiApplication() {
    return qobject_cast<QGuiApplication*>(QCoreApplication::instance());
}

bool isValidAppearance(const VkAppearance appearance) {
    switch (appearance) {
    case VkAppearance::Light:
    case VkAppearance::Dark:
    case VkAppearance::Auto:
        return true;
    }
    return false;
}

bool isValidAccentColor(const VkAccentColor accentColor) {
    switch (accentColor) {
    case VkAccentColor::Blue:
    case VkAccentColor::Purple:
    case VkAccentColor::Pink:
    case VkAccentColor::Red:
    case VkAccentColor::Orange:
    case VkAccentColor::Yellow:
    case VkAccentColor::Green:
    case VkAccentColor::Graphite:
        return true;
    }
    return false;
}

qreal platformWindowCornerRadius() {
#if defined(Q_OS_WIN)
    const QOperatingSystemVersion version = QOperatingSystemVersion::current();
    const bool windows11OrGreater =
        version.majorVersion() > 10 ||
        (version.majorVersion() == 10 && version.microVersion() >= 22000);
    return windows11OrGreater ? 8.0 : 0.0;
#elif defined(Q_OS_MACOS)
    const QOperatingSystemVersion version = QOperatingSystemVersion::current();
    if (version.majorVersion() >= 27) {
        return 16.0;
    }
    if (version.majorVersion() >= 26) {
        return 14.0;
    }
    return 12.0;
#elif defined(Q_OS_LINUX)
    return 10.0;
#else
    return 8.0;
#endif
}

VkAppearance systemAppearance() {
    const auto* application = currentGuiApplication();
    if (application == nullptr) {
        return VkAppearance::Light;
    }

    const Qt::ColorScheme scheme = application->styleHints()->colorScheme();
    if (scheme == Qt::ColorScheme::Dark) {
        return VkAppearance::Dark;
    }
    if (scheme == Qt::ColorScheme::Light) {
        return VkAppearance::Light;
    }

    // Some platform plugins report an unknown scheme. Their initial palette is
    // still a useful, deterministic fallback before vkui applies its palette.
    return application->palette().color(QPalette::Window).lightnessF() < 0.5 ? VkAppearance::Dark
                                                                             : VkAppearance::Light;
}

struct AccentResolution final {
    QColor accent;
    QColor hovered;
    QColor pressed;
    QColor focusRing;
};

AccentResolution makeAccent(const char* accent, const char* hovered, const char* pressed,
                            const int focusAlpha) {
    AccentResolution result{
        QColor(QString::fromLatin1(accent)), QColor(QString::fromLatin1(hovered)),
        QColor(QString::fromLatin1(pressed)), QColor(QString::fromLatin1(accent))};
    result.focusRing.setAlpha(focusAlpha);
    return result;
}

AccentResolution resolveAccent(const VkAccentColor accentColor, const VkAppearance appearance) {
    const bool dark = appearance == VkAppearance::Dark;
    switch (accentColor) {
    case VkAccentColor::Blue:
        return dark ? makeAccent("#0a84ff", "#409cff", "#0070dc", 144)
                    : makeAccent("#007aff", "#006ee6", "#005fc7", 112);
    case VkAccentColor::Purple:
        return dark ? makeAccent("#bf5af2", "#cc74f6", "#a746d6", 144)
                    : makeAccent("#af52de", "#9f48cc", "#893db2", 112);
    case VkAccentColor::Pink:
        return dark ? makeAccent("#ff375f", "#ff6481", "#db294f", 144)
                    : makeAccent("#ff2d55", "#e6294d", "#c92343", 112);
    case VkAccentColor::Red:
        return dark ? makeAccent("#ff453a", "#ff6961", "#d9362e", 144)
                    : makeAccent("#ff3b30", "#e6352b", "#c92e26", 112);
    case VkAccentColor::Orange:
        return dark ? makeAccent("#ff9f0a", "#ffb340", "#d98600", 144)
                    : makeAccent("#ff9500", "#e68600", "#c97500", 112);
    case VkAccentColor::Yellow:
        return dark ? makeAccent("#ffd60a", "#ffe04a", "#d9b600", 144)
                    : makeAccent("#ffcc00", "#e6b800", "#c79f00", 112);
    case VkAccentColor::Green:
        return dark ? makeAccent("#30d158", "#5ddb7b", "#28b34b", 144)
                    : makeAccent("#34c759", "#2fb34f", "#299b45", 112);
    case VkAccentColor::Graphite:
        return dark ? makeAccent("#98989d", "#aeaeb2", "#7f7f84", 144)
                    : makeAccent("#8e8e93", "#7f7f84", "#6e6e73", 112);
    }
    return dark ? makeAccent("#0a84ff", "#409cff", "#0070dc", 144)
                : makeAccent("#007aff", "#006ee6", "#005fc7", 112);
}

VkColorTokens lightColors(const VkAccentColor accentColor) {
    VkColorTokens colors;
    colors.windowBackground = QColor(QStringLiteral("#f5f5f7"));
    colors.contentBackground = QColor(QStringLiteral("#ffffff"));
    colors.elevatedBackground = QColor(QStringLiteral("#fbfbfc"));
    colors.popoverBackground = QColor(QStringLiteral("#fafafb"));

    colors.controlFill = QColor(QStringLiteral("#e9e9ec"));
    colors.controlFillHovered = QColor(QStringLiteral("#e2e2e6"));
    colors.controlFillPressed = QColor(QStringLiteral("#d7d7dc"));
    colors.controlFillDisabled = QColor(QStringLiteral("#efeff1"));

    colors.separator = QColor(QStringLiteral("#d9d9de"));
    colors.border = QColor(QStringLiteral("#d1d1d6"));
    colors.borderStrong = QColor(QStringLiteral("#afafb5"));

    colors.textPrimary = QColor(QStringLiteral("#1d1d1f"));
    colors.textSecondary = QColor(QStringLiteral("#5c5c61"));
    colors.textTertiary = QColor(QStringLiteral("#7d7d83"));
    colors.textDisabled = QColor(QStringLiteral("#a3a3a8"));

    const AccentResolution accent = resolveAccent(accentColor, VkAppearance::Light);
    colors.accent = accent.accent;
    colors.accentHovered = accent.hovered;
    colors.accentPressed = accent.pressed;

    colors.destructive = QColor(QStringLiteral("#d92d20"));
    colors.warning = QColor(QStringLiteral("#a96200"));
    colors.success = QColor(QStringLiteral("#248a3d"));

    colors.focusRing = accent.focusRing;
    colors.shadow = QColor(0, 0, 0, 72);

    colors.symbolPrimary = colors.textPrimary;
    colors.symbolSecondary = colors.textSecondary;
    colors.symbolDisabled = colors.textDisabled;
    return colors;
}

VkColorTokens darkColors(const VkAccentColor accentColor) {
    VkColorTokens colors;
    colors.windowBackground = QColor(QStringLiteral("#1c1c1e"));
    colors.contentBackground = QColor(QStringLiteral("#242426"));
    colors.elevatedBackground = QColor(QStringLiteral("#2c2c2e"));
    colors.popoverBackground = QColor(QStringLiteral("#2b2b2e"));

    colors.controlFill = QColor(QStringLiteral("#363638"));
    colors.controlFillHovered = QColor(QStringLiteral("#414144"));
    colors.controlFillPressed = QColor(QStringLiteral("#4b4b4e"));
    colors.controlFillDisabled = QColor(QStringLiteral("#2b2b2d"));

    colors.separator = QColor(QStringLiteral("#444448"));
    colors.border = QColor(QStringLiteral("#505055"));
    colors.borderStrong = QColor(QStringLiteral("#68686e"));

    colors.textPrimary = QColor(QStringLiteral("#f5f5f7"));
    colors.textSecondary = QColor(QStringLiteral("#b5b5ba"));
    colors.textTertiary = QColor(QStringLiteral("#8d8d93"));
    colors.textDisabled = QColor(QStringLiteral("#66666c"));

    const AccentResolution accent = resolveAccent(accentColor, VkAppearance::Dark);
    colors.accent = accent.accent;
    colors.accentHovered = accent.hovered;
    colors.accentPressed = accent.pressed;

    colors.destructive = QColor(QStringLiteral("#ff453a"));
    colors.warning = QColor(QStringLiteral("#ffd60a"));
    colors.success = QColor(QStringLiteral("#32d74b"));

    colors.focusRing = accent.focusRing;
    colors.shadow = QColor(0, 0, 0, 160);

    colors.symbolPrimary = colors.textPrimary;
    colors.symbolSecondary = colors.textSecondary;
    colors.symbolDisabled = colors.textDisabled;
    return colors;
}

VkMetricTokens defaultMetrics() {
    VkMetricTokens metrics;
    metrics.spacing2 = 2.0;
    metrics.spacing4 = 4.0;
    metrics.spacing6 = 6.0;
    metrics.spacing8 = 8.0;
    metrics.spacing12 = 12.0;
    metrics.spacing16 = 16.0;
    metrics.spacing20 = 20.0;
    metrics.spacing24 = 24.0;

    metrics.controlHeightSmall = 22.0;
    metrics.controlHeightRegular = 28.0;
    metrics.controlHeightLarge = 36.0;

    metrics.fixedControlExtentSmall = 14.0;
    metrics.fixedControlExtentRegular = 18.0;
    metrics.fixedControlExtentLarge = 22.0;

    metrics.cornerRadiusSmall = 5.0;
    metrics.cornerRadiusRegular = 7.0;
    metrics.windowCornerRadius = platformWindowCornerRadius();
    metrics.cornerRadiusLarge = metrics.windowCornerRadius;
    metrics.popoverCornerRadius = metrics.windowCornerRadius;
    metrics.popoverShadowRadius = 24.0;

    metrics.borderWidth = 1.0;
    metrics.focusRingWidth = 3.0;

    metrics.switchTrackWidth = 34.0;
    metrics.switchTrackHeight = 18.0;
    metrics.switchThumbDiameter = 14.0;

    metrics.popoverArrowWidth = 18.0;
    metrics.popoverArrowDepth = 10.0;
    metrics.popoverScreenMargin = 12.0;
    metrics.popoverAnchorGap = 6.0;
    return metrics;
}

QFont adjustedFont(QFont font, const qreal pointDelta, const QFont::Weight weight) {
    if (font.pointSizeF() > 0.0) {
        font.setPointSizeF(std::max(1.0, font.pointSizeF() + pointDelta));
    } else if (font.pixelSize() > 0) {
        font.setPixelSize(std::max(1, font.pixelSize() + qRound(pointDelta)));
    }
    font.setWeight(weight);
    return font;
}

VkTypographyTokens defaultTypography() {
    QFont body;
    QFont caption;
    if (currentGuiApplication() != nullptr) {
        body = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
        caption = QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont);
    }

    VkTypographyTokens typography;
    typography.caption = caption;
    typography.body = body;
    typography.bodyEmphasized = adjustedFont(body, 0.0, QFont::DemiBold);
    typography.headline = adjustedFont(body, 1.0, QFont::DemiBold);
    typography.title = adjustedFont(body, 4.0, QFont::Medium);
    typography.largeTitle = adjustedFont(body, 10.0, QFont::Medium);
    return typography;
}

VkMotionTokens defaultMotion() {
    VkMotionTokens motion;
    motion.immediate = {0, QEasingCurve(QEasingCurve::Linear)};
    motion.stateTransition = {160, QEasingCurve(QEasingCurve::InOutCubic)};
    motion.enter = {180, QEasingCurve(QEasingCurve::OutCubic)};
    motion.exit = {140, QEasingCurve(QEasingCurve::InOutCubic)};
    motion.emphasizedEnter = {220, QEasingCurve(QEasingCurve::OutCubic)};
    motion.emphasizedExit = {160, QEasingCurve(QEasingCurve::InOutCubic)};
    return motion;
}

QColor textSelectionColor(const VkColorTokens& colors, VkAppearance appearance, bool active) {
    QColor color = active ? colors.accent : colors.accentHovered;
    const bool dark = appearance == VkAppearance::Dark;
    color.setAlphaF(dark ? (active ? 0.34 : 0.24) : (active ? 0.22 : 0.16));
    return color;
}

QPalette paletteForColors(const VkColorTokens& colors, VkAppearance appearance) {
    QPalette palette;
    palette.setColor(QPalette::Window, colors.windowBackground);
    palette.setColor(QPalette::WindowText, colors.textPrimary);
    palette.setColor(QPalette::Base, colors.contentBackground);
    palette.setColor(QPalette::AlternateBase, colors.elevatedBackground);
    palette.setColor(QPalette::ToolTipBase, colors.elevatedBackground);
    palette.setColor(QPalette::ToolTipText, colors.textPrimary);
    palette.setColor(QPalette::Text, colors.textPrimary);
    palette.setColor(QPalette::Button, colors.controlFill);
    palette.setColor(QPalette::ButtonText, colors.textPrimary);
    palette.setColor(QPalette::BrightText, colors.destructive);
    palette.setColor(QPalette::Light, colors.elevatedBackground);
    palette.setColor(QPalette::Midlight, colors.separator);
    palette.setColor(QPalette::Dark, colors.borderStrong);
    palette.setColor(QPalette::Mid, colors.border);
    palette.setColor(QPalette::Shadow, colors.shadow);
    palette.setColor(QPalette::Highlight, textSelectionColor(colors, appearance, true));
    palette.setColor(QPalette::HighlightedText, colors.textPrimary);
    palette.setColor(QPalette::Link, colors.accent);
    palette.setColor(QPalette::LinkVisited, colors.accentPressed);
    palette.setColor(QPalette::PlaceholderText, colors.textTertiary);
    palette.setColor(QPalette::Accent, colors.accent);

    palette.setColor(QPalette::Inactive, QPalette::Highlight,
                     textSelectionColor(colors, appearance, false));
    palette.setColor(QPalette::Inactive, QPalette::HighlightedText, colors.textPrimary);

    palette.setColor(QPalette::Disabled, QPalette::WindowText, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Button, colors.controlFillDisabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::BrightText, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, colors.controlFillDisabled);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Link, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::LinkVisited, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Accent, colors.textDisabled);
    return palette;
}

bool colorTokensEqual(const VkColorTokens& a, const VkColorTokens& b) {
    return a.windowBackground == b.windowBackground && a.contentBackground == b.contentBackground &&
           a.elevatedBackground == b.elevatedBackground &&
           a.popoverBackground == b.popoverBackground && a.controlFill == b.controlFill &&
           a.controlFillHovered == b.controlFillHovered &&
           a.controlFillPressed == b.controlFillPressed &&
           a.controlFillDisabled == b.controlFillDisabled && a.separator == b.separator &&
           a.border == b.border && a.borderStrong == b.borderStrong &&
           a.textPrimary == b.textPrimary && a.textSecondary == b.textSecondary &&
           a.textTertiary == b.textTertiary && a.textDisabled == b.textDisabled &&
           a.accent == b.accent && a.accentHovered == b.accentHovered &&
           a.accentPressed == b.accentPressed && a.destructive == b.destructive &&
           a.warning == b.warning && a.success == b.success && a.focusRing == b.focusRing &&
           a.shadow == b.shadow && a.symbolPrimary == b.symbolPrimary &&
           a.symbolSecondary == b.symbolSecondary && a.symbolDisabled == b.symbolDisabled;
}

bool metricTokensEqual(const VkMetricTokens& a, const VkMetricTokens& b) {
    return a.spacing2 == b.spacing2 && a.spacing4 == b.spacing4 && a.spacing6 == b.spacing6 &&
           a.spacing8 == b.spacing8 && a.spacing12 == b.spacing12 && a.spacing16 == b.spacing16 &&
           a.spacing20 == b.spacing20 && a.spacing24 == b.spacing24 &&
           a.controlHeightSmall == b.controlHeightSmall &&
           a.controlHeightRegular == b.controlHeightRegular &&
           a.controlHeightLarge == b.controlHeightLarge &&
           a.fixedControlExtentSmall == b.fixedControlExtentSmall &&
           a.fixedControlExtentRegular == b.fixedControlExtentRegular &&
           a.fixedControlExtentLarge == b.fixedControlExtentLarge &&
           a.cornerRadiusSmall == b.cornerRadiusSmall &&
           a.cornerRadiusRegular == b.cornerRadiusRegular &&
           a.cornerRadiusLarge == b.cornerRadiusLarge &&
           a.windowCornerRadius == b.windowCornerRadius &&
           a.popoverCornerRadius == b.popoverCornerRadius && a.borderWidth == b.borderWidth &&
           a.focusRingWidth == b.focusRingWidth && a.switchTrackWidth == b.switchTrackWidth &&
           a.switchTrackHeight == b.switchTrackHeight &&
           a.switchThumbDiameter == b.switchThumbDiameter &&
           a.popoverArrowWidth == b.popoverArrowWidth &&
           a.popoverArrowDepth == b.popoverArrowDepth &&
           a.popoverScreenMargin == b.popoverScreenMargin &&
           a.popoverAnchorGap == b.popoverAnchorGap &&
           a.popoverShadowRadius == b.popoverShadowRadius;
}

bool typographyTokensEqual(const VkTypographyTokens& a, const VkTypographyTokens& b) {
    return a.caption == b.caption && a.body == b.body && a.bodyEmphasized == b.bodyEmphasized &&
           a.headline == b.headline && a.title == b.title && a.largeTitle == b.largeTitle;
}

bool motionSpecsEqual(const VkMotionSpec& a, const VkMotionSpec& b) {
    return a.durationMs == b.durationMs && a.easing == b.easing;
}

bool motionTokensEqual(const VkMotionTokens& a, const VkMotionTokens& b) {
    return motionSpecsEqual(a.immediate, b.immediate) &&
           motionSpecsEqual(a.stateTransition, b.stateTransition) &&
           motionSpecsEqual(a.enter, b.enter) && motionSpecsEqual(a.exit, b.exit) &&
           motionSpecsEqual(a.emphasizedEnter, b.emphasizedEnter) &&
           motionSpecsEqual(a.emphasizedExit, b.emphasizedExit);
}

} // namespace

VkThemeManagerPrivate::VkThemeManagerPrivate(VkThemeManager* manager)
    : q(manager), resolvedTheme(createTheme(systemAppearance(), requestedAccentColor, 1)) {}

VkAppearance VkThemeManagerPrivate::resolveEffectiveAppearance() const {
    return requestedAppearance == VkAppearance::Auto ? systemAppearance() : requestedAppearance;
}

VkTheme VkThemeManagerPrivate::createTheme(const VkAppearance appearance,
                                           const VkAccentColor accentColor,
                                           const quint64 generation) {
    return VkTheme(appearance == VkAppearance::Dark ? darkColors(accentColor)
                                                    : lightColors(accentColor),
                   defaultMetrics(), defaultTypography(), defaultMotion(), appearance, generation);
}

bool VkThemeManagerPrivate::themesHaveEqualTokens(const VkTheme& left, const VkTheme& right) {
    return left.effectiveAppearance_ == right.effectiveAppearance_ &&
           colorTokensEqual(left.colors_, right.colors_) &&
           metricTokensEqual(left.metrics_, right.metrics_) &&
           typographyTokensEqual(left.typography_, right.typography_) &&
           motionTokensEqual(left.motion_, right.motion_);
}

bool VkThemeManagerPrivate::refreshTheme() {
    const VkAppearance effective = resolveEffectiveAppearance();
    const VkTheme candidate =
        createTheme(effective, requestedAccentColor, resolvedTheme.generation_);
    if (themesHaveEqualTokens(resolvedTheme, candidate)) {
        return false;
    }

    resolvedTheme = createTheme(effective, requestedAccentColor, resolvedTheme.generation_ + 1);
    return true;
}

void VkThemeManagerPrivate::applyPalette() const {
    if (application != nullptr) {
        const QPalette palette =
            paletteForColors(resolvedTheme.colors_, resolvedTheme.effectiveAppearance_);
        if (application->palette() != palette) {
            application->setPalette(palette);
        }
    }
}

void VkThemeManagerPrivate::attachToApplication() {
    QGuiApplication* currentApplication = currentGuiApplication();
    if (currentApplication == nullptr || currentApplication == application) {
        return;
    }

    if (colorSchemeConnection) {
        QObject::disconnect(colorSchemeConnection);
    }

    application = currentApplication;
    styleHints = currentApplication->styleHints();
    colorSchemeConnection =
        QObject::connect(styleHints, &QStyleHints::colorSchemeChanged, q,
                         [this](Qt::ColorScheme) { handleSystemColorSchemeChange(); });

    const VkAppearance previousEffective = resolvedTheme.effectiveAppearance_;
    const bool tokensChanged = refreshTheme();

    // A palette may have been customized between manager construction and the
    // creation of QGuiApplication, so apply it even if token values compare equal.
    applyPalette();
    if (resolvedTheme.effectiveAppearance_ != previousEffective) {
        Q_EMIT q->effectiveAppearanceChanged(resolvedTheme.effectiveAppearance_);
    }
    if (tokensChanged) {
        Q_EMIT q->themeChanged(resolvedTheme.generation_);
    }
}

void VkThemeManagerPrivate::setAppearance(const VkAppearance appearance) {
    if (requestedAppearance == appearance) {
        return;
    }

    const VkAppearance previousEffective = resolvedTheme.effectiveAppearance_;
    requestedAppearance = appearance;
    const bool tokensChanged = refreshTheme();
    applyPalette();

    Q_EMIT q->appearanceChanged(requestedAppearance);
    if (resolvedTheme.effectiveAppearance_ != previousEffective) {
        Q_EMIT q->effectiveAppearanceChanged(resolvedTheme.effectiveAppearance_);
    }
    if (tokensChanged) {
        Q_EMIT q->themeChanged(resolvedTheme.generation_);
    }
}

void VkThemeManagerPrivate::setAccentColor(const VkAccentColor accentColor) {
    if (requestedAccentColor == accentColor) {
        return;
    }

    requestedAccentColor = accentColor;
    const bool tokensChanged = refreshTheme();
    applyPalette();

    Q_EMIT q->accentColorChanged(requestedAccentColor);
    if (tokensChanged) {
        Q_EMIT q->themeChanged(resolvedTheme.generation_);
    }
}

void VkThemeManagerPrivate::handleSystemColorSchemeChange() {
    if (requestedAppearance != VkAppearance::Auto) {
        return;
    }

    const VkAppearance previousEffective = resolvedTheme.effectiveAppearance_;
    if (!refreshTheme()) {
        return;
    }

    applyPalette();
    if (resolvedTheme.effectiveAppearance_ != previousEffective) {
        Q_EMIT q->effectiveAppearanceChanged(resolvedTheme.effectiveAppearance_);
    }
    Q_EMIT q->themeChanged(resolvedTheme.generation_);
}

VkThemeManager* VkThemeManager::instance() {
    static VkThemeManager manager;
    manager.d->attachToApplication();
    return &manager;
}

VkThemeManager::VkThemeManager(QObject* parent)
    : QObject(parent), d(std::make_unique<VkThemeManagerPrivate>(this)) {
    d->attachToApplication();
}

VkThemeManager::~VkThemeManager() = default;

VkAppearance VkThemeManager::appearance() const noexcept {
    return d->requestedAppearance;
}

void VkThemeManager::setAppearance(const VkAppearance appearance) {
    if (!isValidAppearance(appearance)) {
        qWarning("VkThemeManager::setAppearance received an invalid enum value");
        return;
    }
    d->attachToApplication();
    d->setAppearance(appearance);
}

VkAppearance VkThemeManager::effectiveAppearance() const noexcept {
    return d->resolvedTheme.effectiveAppearance();
}

const VkTheme& VkThemeManager::theme() const noexcept {
    return d->resolvedTheme;
}

VkAccentColor VkThemeManager::accentColor() const noexcept {
    return d->requestedAccentColor;
}

void VkThemeManager::setAccentColor(const VkAccentColor accentColor) {
    if (!isValidAccentColor(accentColor)) {
        qWarning("VkThemeManager::setAccentColor received an invalid enum value");
        return;
    }
    d->attachToApplication();
    d->setAccentColor(accentColor);
}

bool VkThemeManager::animationsEnabled() const noexcept {
    return d->animationsEnabled;
}

void VkThemeManager::setAnimationsEnabled(const bool enabled) {
    if (d->animationsEnabled == enabled) {
        return;
    }
    d->animationsEnabled = enabled;
    Q_EMIT animationsEnabledChanged(enabled);
}

} // namespace vkui
