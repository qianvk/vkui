// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QObject>
#include <QtCore/QtTypes>
#include <memory>
#include <vkui/VkUiGlobal.h>
#include <vkui/core/VkAccentColor.h>
#include <vkui/core/VkAppearance.h>
#include <vkui/core/VkTheme.h>

namespace vkui {

class VkThemeManagerPrivate;

/** Owns the process-wide appearance request and resolved semantic theme. */
class VKUI_CORE_EXPORT VkThemeManager final : public QObject {
    Q_OBJECT

  public:
    /**
     * Returns the process-wide manager.
     *
     * The object is library-owned and must not be deleted by callers.
     */
    [[nodiscard]] static VkThemeManager* instance();

    ~VkThemeManager() override;

    [[nodiscard]] VkAppearance appearance() const noexcept;
    void setAppearance(VkAppearance appearance);

    [[nodiscard]] VkAppearance effectiveAppearance() const noexcept;
    [[nodiscard]] const VkTheme& theme() const noexcept;

    [[nodiscard]] VkAccentColor accentColor() const noexcept;
    void setAccentColor(VkAccentColor accentColor);

    [[nodiscard]] bool animationsEnabled() const noexcept;
    void setAnimationsEnabled(bool enabled);

  Q_SIGNALS:
    void appearanceChanged(vkui::VkAppearance appearance);
    void effectiveAppearanceChanged(vkui::VkAppearance appearance);
    void themeChanged(quint64 generation);
    void accentColorChanged(vkui::VkAccentColor accentColor);
    void animationsEnabledChanged(bool enabled);

  private:
    explicit VkThemeManager(QObject* parent = nullptr);

    Q_DISABLE_COPY_MOVE(VkThemeManager)

    std::unique_ptr<VkThemeManagerPrivate> d;
};

} // namespace vkui
