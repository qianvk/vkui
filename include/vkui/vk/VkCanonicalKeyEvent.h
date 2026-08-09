#pragma once

#include "VkInputEngine.h"

#include <cstddef>
#include <memory>

class QKeyEvent;

namespace vkui::vk::detail {

/**
 * Last-value cache for the Qt ingress boundary.
 *
 * Qt sends ShortcutOverride and KeyPress as separate QKeyEvent instances for
 * one physical key. Their event types differ, but their complete key payload
 * is identical. VkCore keeps accepting those original events and uses this
 * private cache so ownership probes and dispatch share one canonical
 * KeySequence instead of independently translating the same payload.
 */
class CanonicalKeyEventCache final
{
public:
    CanonicalKeyEventCache();
    ~CanonicalKeyEventCache();

    CanonicalKeyEventCache(
        const CanonicalKeyEventCache &) = delete;
    CanonicalKeyEventCache &operator=(
        const CanonicalKeyEventCache &) = delete;

    [[nodiscard]] const KeySequence &keys(
        const QKeyEvent &event) const;

    // Internal diagnostics used by focused cache regression tests.
    [[nodiscard]] std::size_t hitCount() const noexcept;
    [[nodiscard]] std::size_t missCount() const noexcept;

private:
    class Implementation;
    std::unique_ptr<Implementation> m_impl;
};

} // namespace vkui::vk::detail
