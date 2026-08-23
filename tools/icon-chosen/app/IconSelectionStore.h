// SPDX-License-Identifier: MIT

#pragma once

#include "IconCatalogModel.h"

#include <QSet>
#include <optional>

class IconSelectionStore final {
  public:
    [[nodiscard]] static QString defaultPath();
    [[nodiscard]] static std::optional<QSet<QString>> load(const QString& path,
                                                           QString* errorMessage = nullptr);
    [[nodiscard]] static bool save(const QString& path, const QList<IconCatalogEntry>& selection);
};
