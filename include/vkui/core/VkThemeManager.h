// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QObject>
#include <QtCore/QtTypes>
#include <memory>
#include <vkui/VkUiGlobal.h>
#include <vkui/core/VkAccentColor.h>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkLiquidGlassPreference.h>
#include <vkui/core/VkTextSize.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeChange.h>

namespace vkui {

class VkThemeManagerPrivate;

/** Owns the process-wide appearance request and resolved semantic theme. */
class VKUI_CORE_EXPORT VkThemeManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int textSizeLevel READ textSizeLevel WRITE setTextSizeLevel RESET resetTextSizeLevel
                   NOTIFY textSizeLevelChanged)
    Q_PROPERTY(bool liquidGlassEnabled READ liquidGlassEnabled WRITE setLiquidGlassEnabled NOTIFY
                   liquidGlassEnabledChanged)
    Q_PROPERTY(int liquidGlassTintLevel READ liquidGlassTintLevel WRITE setLiquidGlassTintLevel
                   RESET resetLiquidGlassTintLevel NOTIFY liquidGlassTintLevelChanged)

  public:
    /**
     * Returns the process-wide manager.
     *
     * The object is library-owned and must not be deleted by callers.
     * Call this API from the future application's GUI thread. Pre-application
     * configuration is supported; ownership transfers to QGuiApplication when
     * it becomes available.
     */
    [[nodiscard]] static VkThemeManager* instance();

    ~VkThemeManager() override;

    [[nodiscard]] VkAppearance appearance() const noexcept;
    void setAppearance(VkAppearance appearance);

    [[nodiscard]] VkAppearance effectiveAppearance() const noexcept;
    [[nodiscard]] const VkTheme& theme() const noexcept;

    [[nodiscard]] VkAccentColor accentColor() const noexcept;
    void setAccentColor(VkAccentColor accentColor);

    /** Returns the canonical application interface-text level. */
    [[nodiscard]] int textSizeLevel() const noexcept;
    /** Applies a clamped discrete text level and its responsive geometry tokens. */
    void setTextSizeLevel(int level);
    /** Restores the platform system typography and default control geometry. */
    void resetTextSizeLevel();

    [[nodiscard]] bool animationsEnabled() const noexcept;
    void setAnimationsEnabled(bool enabled);

    /** Returns whether glass-capable surfaces use the shared optical material. */
    [[nodiscard]] bool liquidGlassEnabled() const noexcept;
    /** Enables or disables Liquid Glass without rebuilding the semantic theme. */
    void setLiquidGlassEnabled(bool enabled);

    /** Returns the process-wide Clear-to-Tinted preference in the inclusive range 0...100. */
    [[nodiscard]] int liquidGlassTintLevel() const noexcept;
    /** Applies a clamped Clear-to-Tinted preference without rebuilding the semantic theme. */
    void setLiquidGlassTintLevel(int level);
    /** Restores the clear Liquid Glass appearance. */
    void resetLiquidGlassTintLevel();

  Q_SIGNALS:
    void appearanceChanged(vkui::VkAppearance appearance);
    void effectiveAppearanceChanged(vkui::VkAppearance appearance);
    /** Reports the new generation and the exact token groups that changed. */
    void themeChanged(quint64 generation, vkui::VkThemeChanges changes);
    void accentColorChanged(vkui::VkAccentColor accentColor);
    void textSizeLevelChanged(int level);
    void animationsEnabledChanged(bool enabled);
    void liquidGlassEnabledChanged(bool enabled);
    void liquidGlassTintLevelChanged(int level);

  private:
    explicit VkThemeManager(QObject* parent = nullptr);

    Q_DISABLE_COPY_MOVE(VkThemeManager)

    std::unique_ptr<VkThemeManagerPrivate> d;
};

} // namespace vkui
