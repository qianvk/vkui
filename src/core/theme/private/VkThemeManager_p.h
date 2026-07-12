// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>
#include <vkui/core/VkAccentColor.h>
#include <vkui/core/VkTheme.h>

namespace vkui {

class VkThemeManager;

class VkThemeManagerPrivate final {
  public:
    explicit VkThemeManagerPrivate(VkThemeManager* manager);

    void attachToApplication();
    void setAppearance(VkAppearance appearance);
    void setAccentColor(VkAccentColor accentColor);
    void handleSystemColorSchemeChange();

    [[nodiscard]] VkAppearance resolveEffectiveAppearance() const;
    [[nodiscard]] bool refreshTheme();
    void applyPalette() const;

    [[nodiscard]] static VkTheme createTheme(VkAppearance appearance, VkAccentColor accentColor,
                                             quint64 generation);
    [[nodiscard]] static bool themesHaveEqualTokens(const VkTheme& left, const VkTheme& right);

    VkThemeManager* q = nullptr;
    VkAppearance requestedAppearance = VkAppearance::Auto;
    VkAccentColor requestedAccentColor = VkAccentColor::Blue;
    VkTheme resolvedTheme;
    bool animationsEnabled = true;
    QPointer<QGuiApplication> application;
    QPointer<QStyleHints> styleHints;
    QMetaObject::Connection colorSchemeConnection;
};

} // namespace vkui
