// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QCache>
#include <QtCore/QMutex>
#include <QtCore/QSize>
#include <QtGui/QImage>
#include <vkui/core/VkIcon.h>

namespace vkui {

struct VkIconMaskCacheKey final {
    VkSymbol symbol = VkSymbol::Close;
    QSize physicalSize;

    friend bool operator==(const VkIconMaskCacheKey&, const VkIconMaskCacheKey&) = default;
};

[[nodiscard]] size_t qHash(const VkIconMaskCacheKey& key, size_t seed = 0) noexcept;

/** Stores color-independent semantic channel masks by symbol and physical raster size. */
class VkIconMaskCache final {
  public:
    [[nodiscard]] static VkIconMaskCache& instance();

    [[nodiscard]] bool lookup(const VkIconMaskCacheKey& key, QImage* result);
    void insert(const VkIconMaskCacheKey& key, const QImage& mask);
    void clear();

  private:
    VkIconMaskCache();

    QMutex mutex_;
    QCache<VkIconMaskCacheKey, QImage> cache_;
};

} // namespace vkui
