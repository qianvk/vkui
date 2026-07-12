// SPDX-License-Identifier: MIT

#include "VkPopoverAnimation_p.h"

#include <QtCore/QParallelAnimationGroup>
#include <QtCore/QSequentialAnimationGroup>
#include <QtCore/QVariantAnimation>
#include <algorithm>
#include <utility>
#include <vkui/core/VkMotion.h>

namespace vkui {

VkPopoverAnimation::VkPopoverAnimation(QObject* parent) : QObject(parent) {}

VkPopoverAnimation::~VkPopoverAnimation() {
    stop();
}

void VkPopoverAnimation::startOpening(qreal initialScale, qreal initialOpacity, bool enabled,
                                      FrameCallback frame, CompletionCallback completed) {
    start(true, initialScale, initialOpacity, enabled, std::move(frame), std::move(completed));
}

void VkPopoverAnimation::startClosing(qreal initialScale, qreal initialOpacity, bool enabled,
                                      FrameCallback frame, CompletionCallback completed) {
    start(false, initialScale, initialOpacity, enabled, std::move(frame), std::move(completed));
}

void VkPopoverAnimation::start(bool opening, qreal initialScale, qreal initialOpacity, bool enabled,
                               FrameCallback frame, CompletionCallback completed) {
    stop();

    // Popovers never animate from zero size. Clamping here also protects
    // interruption paths from an invalid caller-provided visual state.
    scale_ = std::clamp(initialScale, 0.90, 1.10);
    opacity_ = std::clamp(initialOpacity, 0.0, 1.0);
    frame_ = std::move(frame);
    completed_ = std::move(completed);

    const VkMotionSpec spec =
        motionSpec(opening ? VkMotionRole::EmphasizedEnter : VkMotionRole::Exit);
    if (!enabled || spec.durationMs <= 0) {
        scale_ = opening ? 1.0 : 0.975;
        opacity_ = opening ? 1.0 : 0.0;
        if (frame_) {
            frame_(scale_, opacity_);
        }
        CompletionCallback completion = std::move(completed_);
        frame_ = {};
        if (completion) {
            completion();
        }
        return;
    }

    group_ = new QParallelAnimationGroup(this);
    QParallelAnimationGroup* const startedGroup = group_;

    const auto observeScale = [this, startedGroup](QVariantAnimation* scaleAnimation) {
        connect(scaleAnimation, &QVariantAnimation::valueChanged, this,
                [this, startedGroup](const QVariant& value) {
                    if (group_ != startedGroup) {
                        return;
                    }
                    scale_ = value.toReal();
                    if (frame_) {
                        frame_(scale_, opacity_);
                    }
                });
    };

    if (opening) {
        const int approachDuration = std::max(1, spec.durationMs * 3 / 4);
        const int settleDuration = spec.durationMs - approachDuration;
        auto* scaleSequence = new QSequentialAnimationGroup;
        group_->addAnimation(scaleSequence);

        auto* approach = new QVariantAnimation;
        approach->setDuration(approachDuration);
        approach->setEasingCurve(spec.easing);
        approach->setStartValue(scale_);
        // The six-per-mille overshoot is visible without reading as a bounce.
        approach->setEndValue(settleDuration > 0 ? 1.006 : 1.0);
        scaleSequence->addAnimation(approach);
        observeScale(approach);

        if (settleDuration > 0) {
            auto* settle = new QVariantAnimation;
            settle->setDuration(settleDuration);
            settle->setEasingCurve(QEasingCurve::InOutSine);
            settle->setStartValue(1.006);
            settle->setEndValue(1.0);
            scaleSequence->addAnimation(settle);
            observeScale(settle);
        }
    } else {
        auto* scaleAnimation = new QVariantAnimation;
        scaleAnimation->setDuration(spec.durationMs);
        scaleAnimation->setEasingCurve(spec.easing);
        scaleAnimation->setStartValue(scale_);
        scaleAnimation->setEndValue(0.975);
        group_->addAnimation(scaleAnimation);
        observeScale(scaleAnimation);
    }

    auto* opacityAnimation = new QVariantAnimation;
    opacityAnimation->setDuration(spec.durationMs);
    opacityAnimation->setEasingCurve(spec.easing);
    opacityAnimation->setStartValue(opacity_);
    opacityAnimation->setEndValue(opening ? 1.0 : 0.0);
    group_->addAnimation(opacityAnimation);

    connect(opacityAnimation, &QVariantAnimation::valueChanged, this,
            [this, startedGroup](const QVariant& value) {
                if (group_ != startedGroup) {
                    return;
                }
                opacity_ = value.toReal();
                if (frame_) {
                    frame_(scale_, opacity_);
                }
            });
    connect(group_, &QParallelAnimationGroup::finished, this, [this, opening, startedGroup] {
        if (group_ != startedGroup) {
            startedGroup->deleteLater();
            return;
        }
        QParallelAnimationGroup* finishedGroup = startedGroup;
        group_ = nullptr;
        scale_ = opening ? 1.0 : 0.975;
        opacity_ = opening ? 1.0 : 0.0;
        if (frame_) {
            frame_(scale_, opacity_);
        }
        frame_ = {};
        CompletionCallback completion = std::move(completed_);
        if (finishedGroup) {
            finishedGroup->deleteLater();
        }
        if (completion) {
            completion();
        }
    });
    group_->start();
}

void VkPopoverAnimation::stop() {
    if (group_) {
        group_->stop();
        // Interruption can happen from a valueChanged callback. Deferred
        // deletion avoids destroying that callback's sender on its own stack.
        group_->deleteLater();
        group_ = nullptr;
    }
    frame_ = {};
    completed_ = {};
}

bool VkPopoverAnimation::isRunning() const noexcept {
    return group_ != nullptr;
}

} // namespace vkui
