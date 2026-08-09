// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QSize>
#include <QtGui/QColor>
#include <QtGui/QIconEngine>
#include <QtGui/QPixmap>
#include <vkui/core/VkIcon.h>

namespace vkui {

class VkSvgIconEngine final : public QIconEngine {
  public:
    VkSvgIconEngine(VkSymbol symbol, VkIconRole role);
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
    QColor explicitPrimary_;
    QColor explicitSecondary_;
    bool usesExplicitColors_ = false;
    QByteArray source_;
    QSize intrinsicSize_;
};

} // namespace vkui
