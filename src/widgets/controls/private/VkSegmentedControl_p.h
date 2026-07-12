// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QRectF>
#include <QVector>

class QButtonGroup;
class QVariantAnimation;

namespace vkui {

class VkSegmentButton;
class VkSegmentedControl;

class VkSegmentedControlPrivate final : public QObject {
  public:
    explicit VkSegmentedControlPrivate(VkSegmentedControl* owner);

    bool validIndex(int index) const noexcept;
    void updateButtonIds();
    void relayout(bool animateIndicator = false);
    void updateIndicator(bool animated);
    void moveSelection(int visualDelta, bool activate);
    int nextEnabledIndex(int start, int logicalDelta) const;
    void updateFocusProxy();

    bool eventFilter(QObject* watched, QEvent* event) override;

    VkSegmentedControl* q = nullptr;
    QButtonGroup* group = nullptr;
    QVector<VkSegmentButton*> buttons;
    int currentIndex = -1;
    QRectF indicatorRect;
    QVariantAnimation* indicatorAnimation = nullptr;
};

} // namespace vkui
