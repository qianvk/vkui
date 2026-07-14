// SPDX-License-Identifier: MIT

#include "VkStylePainter_p.h"
#include "VkWidgetAnimationManager_p.h"

#include <QAbstractButton>
#include <QEvent>
#include <QFocusEvent>
#include <QHash>
#include <QPointer>
#include <QVariantAnimation>
#include <QWidget>
#include <algorithm>
#include <utility>
#include <vkui/core/VkMotion.h>
#include <vkui/core/VkThemeManager.h>

namespace vkui {

namespace {

constexpr auto channelCount = static_cast<std::size_t>(VkWidgetAnimationManager::Channel::Count);

std::size_t channelIndex(VkWidgetAnimationManager::Channel channel) {
    return static_cast<std::size_t>(channel);
}

} // namespace

struct VkWidgetAnimationManager::WidgetState {
    QPointer<QWidget> widget;
    std::array<qreal, channelCount> values{};
    std::array<qreal, channelCount> targets{};
    std::array<QVariantAnimation*, channelCount> animations{};
    bool observed = false;
    bool keyboardFocusVisible = false;
};

VkWidgetAnimationManager::VkWidgetAnimationManager(QObject* parent) : QObject(parent) {
    auto* themeManager = VkThemeManager::instance();
    connect(themeManager, &VkThemeManager::animationsEnabledChanged, this, [this](bool enabled) {
        if (!enabled) {
            settleAnimations();
        }
    });
}

VkWidgetAnimationManager::~VkWidgetAnimationManager() {
    const auto widgets = m_states.keys();
    for (QWidget* widget : widgets) {
        removeState(widget);
    }
}

void VkWidgetAnimationManager::watch(QWidget* widget) {
    if (!widget) {
        return;
    }

    WidgetState* state = ensureState(widget);
    if (state->observed) {
        return;
    }
    state->values[channelIndex(Channel::Hover)] = widget->underMouse() ? 1.0 : 0.0;
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    if (const auto* button = qobject_cast<const QAbstractButton*>(widget)) {
        state->values[channelIndex(Channel::Press)] = button->isDown() ? 1.0 : 0.0;
        state->values[channelIndex(Channel::Selection)] = button->isChecked() ? 1.0 : 0.0;
    }
#else
    if (const auto* button = qobject_cast<const QAbstractButton*>(widget)) {
        state->values[channelIndex(Channel::Selection)] = button->isChecked() ? 1.0 : 0.0;
    }
#endif
    state->values[channelIndex(Channel::Focus)] =
        widget->hasFocus() && state->keyboardFocusVisible ? 1.0 : 0.0;
    state->targets = state->values;

    widget->setAttribute(Qt::WA_Hover, true);
    widget->installEventFilter(this);
    state->observed = true;

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
        connect(button, &QAbstractButton::pressed, this,
                [this, widget] { setTarget(widget, Channel::Press, 1.0); });
        connect(button, &QAbstractButton::released, this,
                [this, widget] { setTarget(widget, Channel::Press, 0.0); });
        connect(button, &QAbstractButton::toggled, this, [this, widget](bool checked) {
            setTarget(widget, Channel::Selection, checked ? 1.0 : 0.0);
        });
    }
#endif
}

void VkWidgetAnimationManager::unwatch(QWidget* widget) {
    if (!widget) {
        return;
    }
    widget->removeEventFilter(this);
    removeState(widget);
}

VkWidgetAnimationManager::Progress VkWidgetAnimationManager::progress(QWidget* widget, bool hovered,
                                                                      bool pressed, bool focused,
                                                                      bool selected) {
    if (!widget) {
        return {hovered ? 1.0 : 0.0, pressed ? 1.0 : 0.0, focused ? 1.0 : 0.0,
                selected ? 1.0 : 0.0};
    }

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    const auto found = m_states.constFind(widget);
    if (found == m_states.cend() || !found.value()->observed) {
        return {hovered ? 1.0 : 0.0, pressed ? 1.0 : 0.0, focused ? 1.0 : 0.0,
                selected ? 1.0 : 0.0};
    }

    // Painting must be read-only. Retargeting animations from QStyleOption state here can make
    // nested style passes disagree and continuously schedule another frame while the UI is idle.
    const WidgetState* state = found.value();
    const bool managesSelection = qobject_cast<const QAbstractButton*>(widget) != nullptr;

    return {state->values[channelIndex(Channel::Hover)],
            state->values[channelIndex(Channel::Press)],
            state->values[channelIndex(Channel::Focus)],
            managesSelection ? state->values[channelIndex(Channel::Selection)]
                             : (selected ? 1.0 : 0.0)};
#else
    WidgetState* state = ensureState(widget);
    setTarget(widget, Channel::Hover, hovered ? 1.0 : 0.0);
    setTarget(widget, Channel::Press, pressed ? 1.0 : 0.0);
    setTarget(widget, Channel::Focus, focused && state->keyboardFocusVisible ? 1.0 : 0.0);
    setTarget(widget, Channel::Selection, selected ? 1.0 : 0.0);

    return {state->values[channelIndex(Channel::Hover)],
            state->values[channelIndex(Channel::Press)],
            state->values[channelIndex(Channel::Focus)],
            state->values[channelIndex(Channel::Selection)]};
#endif
}

bool VkWidgetAnimationManager::eventFilter(QObject* watched, QEvent* event) {
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget) {
        return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::Enter:
    case QEvent::HoverEnter:
        setTarget(widget, Channel::Hover, 1.0);
        break;
    case QEvent::Leave:
    case QEvent::HoverLeave:
        setTarget(widget, Channel::Hover, 0.0);
        break;
    case QEvent::MouseButtonPress:
        ensureState(widget)->keyboardFocusVisible = false;
        setTarget(widget, Channel::Focus, 0.0);
        setTarget(widget, Channel::Press, 1.0);
        break;
    case QEvent::MouseButtonRelease:
        setTarget(widget, Channel::Press, 0.0);
        break;
    case QEvent::KeyPress:
        if (widget->hasFocus()) {
            ensureState(widget)->keyboardFocusVisible = true;
            setTarget(widget, Channel::Focus, 1.0);
        }
        break;
    case QEvent::FocusIn: {
        const auto* focusEvent = static_cast<QFocusEvent*>(event);
        WidgetState* state = ensureState(widget);
        state->keyboardFocusVisible = VkStylePainter::showsKeyboardFocus(focusEvent->reason());
        setTarget(widget, Channel::Focus, state->keyboardFocusVisible ? 1.0 : 0.0);
        break;
    }
    case QEvent::FocusOut:
        ensureState(widget)->keyboardFocusVisible = false;
        setTarget(widget, Channel::Focus, 0.0);
        setTarget(widget, Channel::Press, 0.0);
        break;
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    case QEvent::Hide:
    case QEvent::WindowDeactivate:
        setTarget(widget, Channel::Hover, 0.0);
        setTarget(widget, Channel::Press, 0.0);
        break;
#endif
    case QEvent::EnabledChange:
        setTarget(widget, Channel::Press, 0.0);
        widget->update();
        break;
    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}

VkWidgetAnimationManager::WidgetState* VkWidgetAnimationManager::ensureState(QWidget* widget) {
    if (auto it = m_states.constFind(widget); it != m_states.constEnd()) {
        return it.value();
    }

    auto* state = new WidgetState;
    state->widget = widget;
    state->keyboardFocusVisible = widget->hasFocus();
    m_states.insert(widget, state);
    connect(widget, &QObject::destroyed, this, [this, widget] { removeState(widget); });
    return state;
}

void VkWidgetAnimationManager::setTarget(QWidget* widget, Channel channel, qreal target) {
    WidgetState* state = ensureState(widget);
    const std::size_t index = channelIndex(channel);
    target = std::clamp(target, 0.0, 1.0);
    if (qFuzzyCompare(state->targets[index] + 1.0, target + 1.0)) {
        return;
    }
    state->targets[index] = target;

    auto* themeManager = VkThemeManager::instance();
    const VkMotionSpec spec = motionSpec(VkMotionRole::StateTransition);
    if (!themeManager->animationsEnabled() || spec.durationMs <= 0 || !widget->isVisible()) {
        if (state->animations[index]) {
            state->animations[index]->stop();
        }
        state->values[index] = target;
        widget->update();
        return;
    }

    QVariantAnimation* animation = state->animations[index];
    if (!animation) {
        animation = new QVariantAnimation(this);
        state->animations[index] = animation;
        connect(animation, &QVariantAnimation::valueChanged, this,
                [state, index](const QVariant& value) {
                    state->values[index] = value.toReal();
                    if (state->widget) {
                        state->widget->update();
                    }
                });
    } else {
        animation->stop();
    }

    // Retarget from the interpolated value so rapidly changing input never jumps.
    animation->setStartValue(state->values[index]);
    animation->setEndValue(target);
    animation->setDuration(spec.durationMs);
    animation->setEasingCurve(spec.easing);
    animation->start();
}

void VkWidgetAnimationManager::removeState(QWidget* widget) {
    WidgetState* state = m_states.take(widget);
    if (!state) {
        return;
    }

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    QObject::disconnect(widget, nullptr, this, nullptr);
#endif

    for (QVariantAnimation* animation : state->animations) {
        if (animation) {
            animation->stop();
            delete animation;
        }
    }
    delete state;
}

void VkWidgetAnimationManager::settleAnimations() {
    for (WidgetState* state : std::as_const(m_states)) {
        for (std::size_t index = 0; index < channelCount; ++index) {
            if (state->animations[index]) {
                state->animations[index]->stop();
            }
            state->values[index] = state->targets[index];
        }
        if (state->widget) {
            state->widget->update();
        }
    }
}

} // namespace vkui
