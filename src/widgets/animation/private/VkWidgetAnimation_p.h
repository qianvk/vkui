// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QPointer>
#include <functional>
#include <vkui/core/VkMotion.h>

class QVariantAnimation;
class QWidget;

namespace vkui {

/** Shared scalar animation policy for stateful VkUI widgets. */
class VkWidgetAnimation final : public QObject {
  public:
    using FrameCallback = std::function<void(qreal)>;
    using CompletionCallback = std::function<void()>;

    explicit VkWidgetAnimation(QWidget* owner, QObject* parent = nullptr);
    ~VkWidgetAnimation() override;

    void start(qreal from, qreal to, VkMotionRole role, FrameCallback frame,
               CompletionCallback completed = {}, qreal durationScale = 1.0);
    void stop();
    void finish();
    [[nodiscard]] bool isRunning() const noexcept;

  protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void complete(bool deliverTarget);

    QPointer<QWidget> owner_;
    QVariantAnimation* animation_ = nullptr;
    FrameCallback frame_;
    CompletionCallback completed_;
    qreal target_ = 0.0;
};

} // namespace vkui
