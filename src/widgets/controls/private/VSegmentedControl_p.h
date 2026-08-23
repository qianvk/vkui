// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QRectF>
#include <QVector>

class QButtonGroup;

namespace vkui {

class VkSegmentButton;
class VSegmentedControl;
class VkWidgetAnimation;

class VSegmentedControlPrivate final : public QObject {
  public:
    explicit VSegmentedControlPrivate(VSegmentedControl* owner);

    bool validIndex(int index) const noexcept;
    void updateButtonIds();
    void relayout(bool animateIndicator = false);
    void updateIndicator(bool animated);
    void moveSelection(int visualDelta, bool activate);
    int nextEnabledIndex(int start, int logicalDelta) const;
    void updateFocusProxy();

    bool eventFilter(QObject* watched, QEvent* event) override;

    VSegmentedControl* q = nullptr;
    QButtonGroup* group = nullptr;
    QVector<VkSegmentButton*> buttons;
    int currentIndex = -1;
    QRectF indicatorRect;
    VkWidgetAnimation* indicatorAnimation = nullptr;
};

} // namespace vkui
