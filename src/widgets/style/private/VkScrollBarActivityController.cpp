// SPDX-License-Identifier: MIT

#include "VkScrollBarActivityController_p.h"

#include <QEvent>
#include <QScrollBar>
#include <QTimerEvent>
#include <algorithm>
#include <vkui/core/VkMotion.h>
#include <vkui/core/VkThemeManager.h>

namespace {

constexpr int activityHoldDurationMs = 700;
constexpr int animationFrameIntervalMs = 16;

} // namespace

namespace vkui {

VkScrollBarActivityController::VkScrollBarActivityController(QObject* parent) : QObject(parent) {
    clock_.start();
    connect(VkThemeManager::instance(), &VkThemeManager::animationsEnabledChanged, this,
            [this](bool enabled) {
                if (enabled) {
                    return;
                }
                for (auto iterator = states_.begin(); iterator != states_.end(); ++iterator) {
                    State& state = iterator.value();
                    state.transitionDurationMs = 0;
                    state.opacity = state.transitionTargetOpacity;
                    iterator.key()->update();
                }
            });
}

VkScrollBarActivityController::~VkScrollBarActivityController() = default;

void VkScrollBarActivityController::polish(QScrollBar* scrollBar) {
    if (!scrollBar || states_.contains(scrollBar)) {
        return;
    }

    states_.insert(scrollBar, {});
    scrollBar->installEventFilter(this);
    connect(scrollBar, &QScrollBar::valueChanged, this,
            [this, scrollBar] { reveal(*scrollBar, true); });
    connect(scrollBar, &QScrollBar::rangeChanged, this,
            [this, scrollBar](int minimum, int maximum) {
                auto iterator = states_.find(scrollBar);
                if (iterator == states_.end()) {
                    return;
                }
                if (minimum >= maximum) {
                    const bool hovered = iterator->hovered;
                    iterator.value() = {};
                    iterator->hovered = hovered;
                    scrollBar->update();
                } else if (iterator->hovered) {
                    reveal(*scrollBar, false);
                }
            });
    connect(scrollBar, &QScrollBar::sliderPressed, this, [this, scrollBar] {
        auto iterator = states_.find(scrollBar);
        if (iterator == states_.end()) {
            return;
        }
        iterator->dragging = true;
        reveal(*scrollBar, false);
    });
    connect(scrollBar, &QScrollBar::sliderReleased, this, [this, scrollBar] {
        auto iterator = states_.find(scrollBar);
        if (iterator == states_.end()) {
            return;
        }
        iterator->dragging = false;
        scheduleHide(*scrollBar);
    });
    connect(scrollBar, &QObject::destroyed, this,
            [this, scrollBar] { states_.remove(scrollBar); });
}

void VkScrollBarActivityController::unpolish(QScrollBar* scrollBar) {
    if (!scrollBar || !states_.remove(scrollBar)) {
        return;
    }
    scrollBar->removeEventFilter(this);
    disconnect(scrollBar, nullptr, this, nullptr);
}

qreal VkScrollBarActivityController::opacity(const QScrollBar* scrollBar) const noexcept {
    const auto iterator = states_.constFind(const_cast<QScrollBar*>(scrollBar));
    return iterator == states_.constEnd() ? 0.0 : iterator->opacity;
}

bool VkScrollBarActivityController::eventFilter(QObject* watched, QEvent* event) {
    auto* scrollBar = qobject_cast<QScrollBar*>(watched);
    auto iterator = states_.find(scrollBar);
    if (!scrollBar || !event || iterator == states_.end()) {
        return QObject::eventFilter(watched, event);
    }

    State& state = iterator.value();
    switch (event->type()) {
    case QEvent::Enter:
    case QEvent::HoverEnter:
        state.hovered = true;
        reveal(*scrollBar, false);
        break;
    case QEvent::Leave:
    case QEvent::HoverLeave:
        state.hovered = false;
        scheduleHide(*scrollBar);
        break;
    case QEvent::MouseButtonPress:
        state.dragging = true;
        reveal(*scrollBar, false);
        break;
    case QEvent::MouseButtonRelease:
        state.dragging = false;
        scheduleHide(*scrollBar);
        break;
    case QEvent::Hide:
        state = {};
        break;
    case QEvent::Show:
        state.hovered = scrollBar->underMouse();
        if (state.hovered) {
            reveal(*scrollBar, false);
        }
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void VkScrollBarActivityController::timerEvent(QTimerEvent* event) {
    if (!event || event->timerId() != timer_.timerId()) {
        QObject::timerEvent(event);
        return;
    }

    const qint64 now = nowMs();
    bool timerStillNeeded = false;
    for (auto iterator = states_.begin(); iterator != states_.end(); ++iterator) {
        QScrollBar* const scrollBar = iterator.key();
        State& state = iterator.value();
        if (state.transitionDurationMs > 0) {
            const qreal progress = std::clamp(
                qreal(now - state.transitionStartMs) / qreal(state.transitionDurationMs), 0.0,
                1.0);
            const qreal easedProgress = state.transitionEasing.valueForProgress(progress);
            state.opacity = state.transitionStartOpacity +
                            (state.transitionTargetOpacity - state.transitionStartOpacity) *
                                easedProgress;
            scrollBar->update();
            if (progress >= 1.0) {
                state.opacity = state.transitionTargetOpacity;
                state.transitionDurationMs = 0;
            } else {
                timerStillNeeded = true;
            }
        }

        if (state.transitionDurationMs == 0 && state.opacity > 0.0 && !state.hovered &&
            !state.dragging) {
            if (now >= state.holdUntilMs) {
                startTransition(*scrollBar, state, 0.0);
            } else {
                timerStillNeeded = true;
            }
        }
        timerStillNeeded = timerStillNeeded || state.transitionDurationMs > 0;
    }

    if (!timerStillNeeded) {
        timer_.stop();
    }
}

qint64 VkScrollBarActivityController::nowMs() const noexcept {
    return clock_.elapsed();
}

void VkScrollBarActivityController::reveal(QScrollBar& scrollBar,
                                           const bool transientActivity) {
    auto iterator = states_.find(&scrollBar);
    if (iterator == states_.end() || !scrollBar.isVisible() ||
        scrollBar.minimum() >= scrollBar.maximum()) {
        return;
    }

    State& state = iterator.value();
    if (transientActivity) {
        state.holdUntilMs = nowMs() + activityHoldDurationMs;
    }
    startTransition(scrollBar, state, 1.0);
    if (transientActivity) {
        ensureTimerRunning();
    }
}

void VkScrollBarActivityController::scheduleHide(QScrollBar& scrollBar) {
    auto iterator = states_.find(&scrollBar);
    if (iterator == states_.end() || iterator->hovered || iterator->dragging) {
        return;
    }

    State& state = iterator.value();
    if (state.holdUntilMs > nowMs()) {
        ensureTimerRunning();
        return;
    }
    startTransition(scrollBar, state, 0.0);
}

void VkScrollBarActivityController::startTransition(QScrollBar& scrollBar, State& state,
                                                    const qreal targetOpacity) {
    if (qFuzzyCompare(state.transitionTargetOpacity + 1.0, targetOpacity + 1.0) &&
        (state.transitionDurationMs > 0 || qFuzzyCompare(state.opacity + 1.0,
                                                        targetOpacity + 1.0))) {
        return;
    }

    const VkMotionSpec motion =
        motionSpec(targetOpacity > state.opacity ? VkMotionRole::Enter : VkMotionRole::Exit);
    state.transitionStartOpacity = state.opacity;
    state.transitionTargetOpacity = targetOpacity;
    state.transitionStartMs = nowMs();
    state.transitionDurationMs = motion.durationMs;
    state.transitionEasing = motion.easing;
    if (motion.durationMs <= 0) {
        state.opacity = targetOpacity;
    } else {
        ensureTimerRunning();
    }
    scrollBar.update();
}

void VkScrollBarActivityController::ensureTimerRunning() {
    if (!timer_.isActive()) {
        timer_.start(animationFrameIntervalMs, Qt::PreciseTimer, this);
    }
}

} // namespace vkui
