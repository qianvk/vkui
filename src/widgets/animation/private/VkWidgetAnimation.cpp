// SPDX-License-Identifier: MIT

#include "VkWidgetAnimation_p.h"

#include <QAbstractAnimation>
#include <QEvent>
#include <QVariantAnimation>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vkui/core/VkThemeManager.h>

namespace vkui {

VkWidgetAnimation::VkWidgetAnimation(QWidget* owner, QObject* parent)
    : QObject(parent), owner_(owner), animation_(new QVariantAnimation(this)) {
    Q_ASSERT(owner_);
    owner_->installEventFilter(this);
    connect(animation_, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        if (frame_) {
            frame_(value.toReal());
        }
    });
    connect(animation_, &QVariantAnimation::finished, this, [this] { complete(true); });
    connect(VkThemeManager::instance(), &VkThemeManager::animationsEnabledChanged, this,
            [this](bool enabled) {
                if (!enabled) {
                    finish();
                }
            });
}

VkWidgetAnimation::~VkWidgetAnimation() {
    if (owner_) {
        owner_->removeEventFilter(this);
    }
}

void VkWidgetAnimation::start(qreal from, qreal to, VkMotionRole role, FrameCallback frame,
                              CompletionCallback completed, qreal durationScale) {
    stop();
    frame_ = std::move(frame);
    completed_ = std::move(completed);
    target_ = to;

    const VkMotionSpec spec = motionSpec(role);
    if (!owner_ || !owner_->isVisible() || spec.durationMs <= 0 ||
        qFuzzyCompare(from + 1.0, to + 1.0)) {
        complete(true);
        return;
    }

    const qreal distanceScale = std::clamp(std::abs(to - from), 0.35, 1.0);
    animation_->setStartValue(from);
    animation_->setEndValue(to);
    animation_->setDuration(
        std::max(1, qRound(spec.durationMs * std::max<qreal>(0.0, durationScale) * distanceScale)));
    animation_->setEasingCurve(spec.easing);
    animation_->start();
}

void VkWidgetAnimation::stop() {
    if (animation_->state() != QAbstractAnimation::Stopped) {
        animation_->stop();
    }
    frame_ = {};
    completed_ = {};
}

void VkWidgetAnimation::finish() {
    if (!isRunning()) {
        return;
    }
    animation_->stop();
    complete(true);
}

bool VkWidgetAnimation::isRunning() const noexcept {
    return animation_->state() == QAbstractAnimation::Running;
}

bool VkWidgetAnimation::eventFilter(QObject* watched, QEvent* event) {
    if (watched == owner_ && event && event->type() == QEvent::Hide) {
        finish();
    }
    return QObject::eventFilter(watched, event);
}

void VkWidgetAnimation::complete(bool deliverTarget) {
    FrameCallback frame = std::move(frame_);
    CompletionCallback completed = std::move(completed_);
    if (deliverTarget && frame) {
        QPointer<VkWidgetAnimation> guard(this);
        frame(target_);
        if (!guard) {
            return;
        }
    }
    if (completed) {
        completed();
    }
}

} // namespace vkui
