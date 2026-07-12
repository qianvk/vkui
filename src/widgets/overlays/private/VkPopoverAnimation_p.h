// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QObject>
#include <functional>

class QParallelAnimationGroup;

namespace vkui {

/** Interruption-safe private scale/fade driver used by VkPopover. */
class VkPopoverAnimation final : public QObject {
  public:
    using FrameCallback = std::function<void(qreal scale, qreal opacity)>;
    using CompletionCallback = std::function<void()>;

    explicit VkPopoverAnimation(QObject* parent = nullptr);
    ~VkPopoverAnimation() override;

    void startOpening(qreal initialScale, qreal initialOpacity, bool enabled, FrameCallback frame,
                      CompletionCallback completed);
    void startClosing(qreal initialScale, qreal initialOpacity, bool enabled, FrameCallback frame,
                      CompletionCallback completed);
    void stop();

    [[nodiscard]] bool isRunning() const noexcept;

  private:
    void start(bool opening, qreal initialScale, qreal initialOpacity, bool enabled,
               FrameCallback frame, CompletionCallback completed);

    QParallelAnimationGroup* group_ = nullptr;
    FrameCallback frame_;
    CompletionCallback completed_;
    qreal scale_ = 1.0;
    qreal opacity_ = 1.0;
};

} // namespace vkui
