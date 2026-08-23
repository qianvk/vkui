// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QObject>
#include <QtCore/QtTypes>
#include <memory>
#include <vkui/VkUiGlobal.h>
#include <vkui/core/VkAccentColor.h>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeChange.h>

namespace vkui {

class VkThemeManagerPrivate;

inline constexpr qreal VkMinimumTextScale = 0.80;
inline constexpr qreal VkDefaultTextScale = 1.00;
inline constexpr qreal VkMaximumTextScale = 1.60;
inline constexpr qreal VkTextScaleStep = 0.05;

/** Owns the process-wide appearance request and resolved semantic theme. */
class VKUI_CORE_EXPORT VkThemeManager final : public QObject {
    Q_OBJECT
    Q_PROPERTY(qreal textScale READ textScale WRITE setTextScale RESET resetTextScale NOTIFY
                   textScaleChanged)

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

    /** Returns the canonical application interface-text scale. */
    [[nodiscard]] qreal textScale() const noexcept;
    /** Applies a clamped, five-percent text scale and its responsive geometry tokens. */
    void setTextScale(qreal scale);
    /** Restores the platform system typography and default control geometry. */
    void resetTextScale();

    [[nodiscard]] bool animationsEnabled() const noexcept;
    void setAnimationsEnabled(bool enabled);

  Q_SIGNALS:
    void appearanceChanged(vkui::VkAppearance appearance);
    void effectiveAppearanceChanged(vkui::VkAppearance appearance);
    /** Reports the new generation and the exact token groups that changed. */
    void themeChanged(quint64 generation, vkui::VkThemeChanges changes);
    void accentColorChanged(vkui::VkAccentColor accentColor);
    void textScaleChanged(qreal scale);
    void animationsEnabledChanged(bool enabled);

  private:
    explicit VkThemeManager(QObject* parent = nullptr);

    Q_DISABLE_COPY_MOVE(VkThemeManager)

    std::unique_ptr<VkThemeManagerPrivate> d;
};

} // namespace vkui
