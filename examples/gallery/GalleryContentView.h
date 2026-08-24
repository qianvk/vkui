// SPDX-License-Identifier: MIT

#pragma once

#include <QWidget>

class QScrollArea;
class QStackedWidget;
class QHBoxLayout;

namespace vkui {
class VLiquidGlassBackdrop;
}

/** Gallery page stack with a transparent title-bar overlay. */
class GalleryContentView final : public QWidget {
  public:
    static constexpr int TitleBarHeight = 56;

    explicit GalleryContentView(QWidget* parent = nullptr);

    [[nodiscard]] QWidget* titleBar() const noexcept;
    [[nodiscard]] QHBoxLayout* titleBarLayout() const noexcept;
    [[nodiscard]] vkui::VLiquidGlassBackdrop* liquidGlassBackdrop() const noexcept;
    void addPage(QWidget* page);
    [[nodiscard]] int count() const;
    [[nodiscard]] int currentIndex() const;
    void setCurrentIndex(int index);
    [[nodiscard]] QScrollArea* pageScrollArea(int index) const;

  private:
    QWidget* titleBar_ = nullptr;
    QHBoxLayout* titleBarLayout_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    vkui::VLiquidGlassBackdrop* liquidGlassBackdrop_ = nullptr;
};
