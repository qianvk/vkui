// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtGui/QFont>
#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>
#include <vkui/core/VkAccentColor.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeChange.h>

namespace vkui {

class VkThemeManager;

class VkThemeManagerPrivate final {
  public:
    explicit VkThemeManagerPrivate(VkThemeManager* manager);

    void attachToApplication();
    void setAppearance(VkAppearance appearance);
    void setAccentColor(VkAccentColor accentColor);
    void setTextScale(qreal scale);
    void handleSystemColorSchemeChange();

    [[nodiscard]] VkAppearance resolveEffectiveAppearance() const;
    [[nodiscard]] VkThemeChanges refreshTheme();
    void applyPalette() const;
    void applyFont() const;

    [[nodiscard]] static VkTheme createTheme(VkAppearance appearance, VkAccentColor accentColor,
                                             qreal textScale, const QFont& baseBodyFont,
                                             const QFont& baseCaptionFont, quint64 generation,
                                             quint64 colorGeneration);
    [[nodiscard]] static VkThemeChanges changedTokenGroups(const VkTheme& previous,
                                                           const VkTheme& candidate);

    VkThemeManager* q = nullptr;
    VkAppearance requestedAppearance = VkAppearance::Auto;
    VkAccentColor requestedAccentColor = VkAccentColor::Blue;
    qreal requestedTextScale = 1.0;
    QFont baseBodyFont;
    QFont baseCaptionFont;
    VkTheme resolvedTheme;
    bool animationsEnabled = true;
    QPointer<QGuiApplication> application;
    QPointer<QStyleHints> styleHints;
    QMetaObject::Connection colorSchemeConnection;
};

} // namespace vkui
