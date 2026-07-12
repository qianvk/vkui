// SPDX-License-Identifier: MIT

#include "VkIconCache_p.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QHashFunctions>
#include <QtCore/QMutexLocker>
#include <QtCore/QObject>
#include <algorithm>
#include <limits>

namespace vkui {

size_t qHash(const VkIconCacheKey& key, size_t seed) noexcept {
    return qHashMulti(seed, static_cast<int>(key.symbol), static_cast<int>(key.role), key.size,
                      key.devicePixelRatio, static_cast<int>(key.mode), static_cast<int>(key.state),
                      key.themeGeneration);
}

VkIconCache& VkIconCache::instance() {
    static VkIconCache cache;
    return cache;
}

VkIconCache::VkIconCache() : cache_(16 * 1024) {
    if (QCoreApplication* application = QCoreApplication::instance()) {
        QObject::connect(application, &QCoreApplication::aboutToQuit, application,
                         [this] { clear(); });
    }
}

void VkIconCache::synchronizeGeneration(const quint64 generation) {
    // Generation changes are infrequent. Clearing eagerly keeps stale pixmaps
    // from occupying memory while the generation in each key prevents reuse.
    if (generation_ != generation) {
        cache_.clear();
        generation_ = generation;
    }
}

bool VkIconCache::lookup(const VkIconCacheKey& key, QPixmap* result) {
    if (result == nullptr) {
        return false;
    }

    const QMutexLocker locker(&mutex_);
    synchronizeGeneration(key.themeGeneration);
    const QPixmap* cached = cache_.object(key);
    if (cached == nullptr) {
        return false;
    }
    *result = *cached;
    return true;
}

void VkIconCache::insert(const VkIconCacheKey& key, const QPixmap& pixmap) {
    if (pixmap.isNull()) {
        return;
    }

    const qint64 bytes =
        static_cast<qint64>(pixmap.width()) * pixmap.height() * std::max(1, pixmap.depth()) / 8;
    const qint64 kibibytes = std::max<qint64>(1, (bytes + 1023) / 1024);
    const int cost = static_cast<int>(std::min<qint64>(kibibytes, std::numeric_limits<int>::max()));

    const QMutexLocker locker(&mutex_);
    synchronizeGeneration(key.themeGeneration);
    cache_.insert(key, new QPixmap(pixmap), cost);
}

void VkIconCache::clear() {
    const QMutexLocker locker(&mutex_);
    cache_.clear();
    generation_ = 0;
}

} // namespace vkui
