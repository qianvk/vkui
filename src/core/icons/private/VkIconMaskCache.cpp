// SPDX-License-Identifier: MIT

#include "VkIconMaskCache_p.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QHashFunctions>
#include <QtCore/QMutexLocker>
#include <QtCore/QObject>
#include <algorithm>
#include <limits>

namespace vkui {

size_t qHash(const VkIconMaskCacheKey& key, const size_t seed) noexcept {
    return qHashMulti(seed, static_cast<int>(key.symbol), key.physicalSize);
}

VkIconMaskCache& VkIconMaskCache::instance() {
    static VkIconMaskCache cache;
    return cache;
}

VkIconMaskCache::VkIconMaskCache() : cache_(8 * 1024) {
    if (QCoreApplication* application = QCoreApplication::instance()) {
        QObject::connect(application, &QCoreApplication::aboutToQuit, application,
                         [this] { clear(); });
    }
}

bool VkIconMaskCache::lookup(const VkIconMaskCacheKey& key, QImage* result) {
    if (result == nullptr) {
        return false;
    }
    const QMutexLocker locker(&mutex_);
    const QImage* cached = cache_.object(key);
    if (cached == nullptr) {
        return false;
    }
    *result = *cached;
    return true;
}

void VkIconMaskCache::insert(const VkIconMaskCacheKey& key, const QImage& mask) {
    if (mask.isNull()) {
        return;
    }
    const qint64 kibibytes = std::max<qint64>(1, (mask.sizeInBytes() + 1023) / 1024);
    const int cost = static_cast<int>(std::min<qint64>(kibibytes, std::numeric_limits<int>::max()));
    const QMutexLocker locker(&mutex_);
    cache_.insert(key, new QImage(mask), cost);
}

void VkIconMaskCache::clear() {
    const QMutexLocker locker(&mutex_);
    cache_.clear();
}

} // namespace vkui
