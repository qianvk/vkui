// SPDX-License-Identifier: MIT

#include "VkSvgIconSourceCache_p.h"

#include "VkSymbolRegistry_p.h"

#include <QtCore/QFile>
#include <QtCore/QString>
#include <QtSvg/QSvgRenderer>
#include <array>
#include <mutex>

namespace vkui {
namespace {

struct CacheEntry final {
    std::once_flag initialization;
    VkSvgIconSourceCache::Source source;
};

constexpr std::size_t kSymbolCount = static_cast<std::size_t>(VkSymbol::Count);
static_assert(detail::kVkSymbolAssets.size() == kSymbolCount);

} // namespace

VkSvgIconSourceCache::Source VkSvgIconSourceCache::source(const VkSymbol symbol) {
    const auto index = static_cast<std::size_t>(symbol);
    if (index >= kSymbolCount) {
        return {};
    }

    static std::array<CacheEntry, kSymbolCount> cache;
    CacheEntry& entry = cache[index];
    std::call_once(entry.initialization, [index, &entry] {
        const std::string_view asset = detail::kVkSymbolAssets[index];
        const QString resourcePath = QStringLiteral(":/vkui/icons/") +
                                     QString::fromLatin1(asset.data(),
                                                         static_cast<qsizetype>(asset.size()));
        QFile file(resourcePath);
        if (!file.open(QIODevice::ReadOnly)) {
            return;
        }

        auto data = std::make_shared<VkSvgIconSourceData>();
        data->source = file.readAll();
        const QSvgRenderer renderer(data->source);
        if (!renderer.isValid()) {
            return;
        }
        data->intrinsicSize = renderer.defaultSize();
        if (data->intrinsicSize.isEmpty()) {
            data->intrinsicSize = QSize(24, 24);
        }
        entry.source = std::move(data);
    });
    return entry.source;
}

} // namespace vkui
