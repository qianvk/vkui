// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QSize>
#include <memory>
#include <vkui/core/VkIcon.h>

namespace vkui {

struct VkSvgIconSourceData final {
    QByteArray source;
    QSize intrinsicSize;
};

/** Owns the immutable SVG source and metadata shared by every icon engine clone. */
class VkSvgIconSourceCache final {
  public:
    using Source = std::shared_ptr<const VkSvgIconSourceData>;

    [[nodiscard]] static Source source(VkSymbol symbol);
};

} // namespace vkui
