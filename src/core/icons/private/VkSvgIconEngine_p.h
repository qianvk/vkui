// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QSize>
#include <QtGui/QColor>
#include <QtGui/QIconEngine>
#include <QtGui/QPalette>
#include <QtGui/QPixmap>
#include <memory>
#include <vkui/core/VkIcon.h>

namespace vkui {

struct VkSvgIconSourceData;

class VkSvgIconEngine final : public QIconEngine {
  public:
    VkSvgIconEngine(VkSymbol symbol, VkIconRole role);
    VkSvgIconEngine(VkSymbol symbol, QPalette::ColorRole role, QPalette::ColorGroup group);
    VkSvgIconEngine(VkSymbol symbol, QColor primary, QColor secondary);

    [[nodiscard]] QIconEngine* clone() const override;
    [[nodiscard]] QString key() const override;
    [[nodiscard]] QSize actualSize(const QSize& size, QIcon::Mode mode,
                                   QIcon::State state) override;
    [[nodiscard]] QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override;
    [[nodiscard]] QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state,
                                       qreal scale) override;
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override;

  private:
    [[nodiscard]] QPixmap renderPixmap(const QSize& size, qreal devicePixelRatio, QIcon::Mode mode,
                                       QIcon::State state) const;

    VkSymbol symbol_;
    VkIconRole role_;
    QPalette::ColorRole paletteRole_ = QPalette::Text;
    QPalette::ColorGroup paletteGroup_ = QPalette::Active;
    QColor explicitPrimary_;
    QColor explicitSecondary_;
    bool usesApplicationPalette_ = false;
    bool usesExplicitColors_ = false;
    std::shared_ptr<const VkSvgIconSourceData> source_;
};

} // namespace vkui
