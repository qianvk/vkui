// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QCache>
#include <QtCore/QMutex>
#include <QtCore/QSize>
#include <QtGui/QPixmap>
#include <vkui/core/VkIcon.h>

namespace vkui {

struct VkIconCacheKey final {
    VkSymbol symbol = VkSymbol::Close;
    VkIconRole role = VkIconRole::Primary;
    QSize size;
    qint64 devicePixelRatio = 1024;
    QIcon::Mode mode = QIcon::Normal;
    QIcon::State state = QIcon::Off;
    quint64 colorGeneration = 0;
    quint64 colorIdentity = 0;

    friend bool operator==(const VkIconCacheKey&, const VkIconCacheKey&) = default;
};

[[nodiscard]] size_t qHash(const VkIconCacheKey& key, size_t seed = 0) noexcept;

class VkIconCache final {
  public:
    [[nodiscard]] static VkIconCache& instance();

    [[nodiscard]] bool lookup(const VkIconCacheKey& key, QPixmap* result);
    void insert(const VkIconCacheKey& key, const QPixmap& pixmap);
    void clear();

  private:
    VkIconCache();

    QMutex mutex_;
    QCache<VkIconCacheKey, QPixmap> cache_;
};

} // namespace vkui
