// SPDX-License-Identifier: MIT

#include "../style/private/VkStylePainter_p.h"
#include "../animation/private/VkWidgetAnimation_p.h"
#include "private/VkSwitch_p.h"

#include <QAccessible>
#include <QAccessibleEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <vkui/core/VkMotion.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/controls/VkSwitch.h>

namespace vkui {

VkSwitchPrivate::VkSwitchPrivate(VkSwitch* owner)
    : q(owner), m_thumbAnimation(new VkWidgetAnimation(owner)) {}

VkSwitchPrivate::~VkSwitchPrivate() {
    delete m_thumbAnimation;
}

void VkSwitchPrivate::animateThumb(bool checked) {
    const qreal target = checked ? 1.0 : 0.0;
    m_thumbAnimation->start(
        thumbProgress, target, VkMotionRole::StateTransition,
        [this](qreal value) {
            thumbProgress = value;
            q->update();
        },
        {}, 1.75);
}

void VkSwitchPrivate::setHovered(bool hovered) {
    setInteractionValue(hoverProgress, hovered);
}

void VkSwitchPrivate::setPressed(bool pressed) {
    setInteractionValue(pressProgress, pressed);
}

void VkSwitchPrivate::setInteractionValue(qreal& value, bool active) {
    const qreal target = active ? 1.0 : 0.0;
    if (qFuzzyCompare(value + 1.0, target + 1.0)) {
        return;
    }
    value = target;
    q->update();
}

VkSwitch::VkSwitch(QWidget* parent)
    : QAbstractButton(parent), d(std::make_unique<VkSwitchPrivate>(this)) {
    QAbstractButton::setCheckable(true);
    setAutoExclusive(false);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_Hover, true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setProperty("_vkui_controlSize", static_cast<int>(d->controlSize));

    connect(this, &QAbstractButton::pressed, this, [this] { d->setPressed(true); });
    connect(this, &QAbstractButton::released, this, [this] { d->setPressed(false); });
    connect(this, &QAbstractButton::toggled, this, [this](bool checked) {
        d->animateThumb(checked);

        // QAbstractButton already exposes checkable/checked state. This explicit
        // notification keeps assistive clients synchronized during animation.
        QAccessible::State changedState;
        changedState.checked = true;
        QAccessibleStateChangeEvent accessibilityEvent(this, changedState);
        QAccessible::updateAccessibility(&accessibilityEvent);
    });
    connect(VkThemeManager::instance(), &VkThemeManager::themeChanged, this, [this](quint64) {
        updateGeometry();
        update();
    });
}

VkSwitch::~VkSwitch() = default;

VkControlSize VkSwitch::controlSize() const noexcept {
    return d->controlSize;
}

void VkSwitch::setControlSize(VkControlSize size) {
    const bool presetChanged = d->controlSize != size;
    const int previousExtent = controlExtent();
    if (!presetChanged && !d->customExtent) {
        return;
    }
    d->controlSize = size;
    d->customExtent.reset();
    setProperty("_vkui_controlSize", static_cast<int>(size));
    setProperty("_vkui_controlExtent", QVariant{});
    updateGeometry();
    update();
    if (presetChanged) {
        emit controlSizeChanged(size);
    }
    if (previousExtent != controlExtent()) {
        emit controlExtentChanged(controlExtent());
    }
}

int VkSwitch::controlExtent() const noexcept {
    return d->customExtent.value_or(vkui::controlExtent(d->controlSize));
}

void VkSwitch::setControlExtent(int logicalPixels) {
    const int normalized =
        std::clamp(logicalPixels, VkMinimumControlExtent, VkMaximumControlExtent);
    if (d->customExtent == normalized) {
        return;
    }
    d->customExtent = normalized;
    setProperty("_vkui_controlExtent", normalized);
    updateGeometry();
    update();
    emit controlExtentChanged(normalized);
}

void VkSwitch::resetControlExtent() {
    if (!d->customExtent) {
        return;
    }
    d->customExtent.reset();
    setProperty("_vkui_controlExtent", QVariant{});
    updateGeometry();
    update();
    emit controlExtentChanged(controlExtent());
}

QSize VkSwitch::sizeHint() const {
    const auto& metrics = VkThemeManager::instance()->theme().metrics();
    const qreal scale = static_cast<qreal>(controlExtent()) / metrics.switchTrackHeight;
    const qreal focusMargin =
        std::ceil(std::max<qreal>(1.0, metrics.borderWidth) + metrics.spacing2);
    return QSize(qCeil(metrics.switchTrackWidth * scale + focusMargin * 2.0),
                 qCeil(metrics.switchTrackHeight * scale + focusMargin * 2.0));
}

QSize VkSwitch::minimumSizeHint() const {
    return sizeHint();
}

void VkSwitch::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)

    const VkTheme& theme = VkThemeManager::instance()->theme();
    const auto& colors = theme.colors();
    const auto& metrics = theme.metrics();
    const qreal scale = static_cast<qreal>(controlExtent()) / metrics.switchTrackHeight;
    const qreal trackWidth = metrics.switchTrackWidth * scale;
    const qreal trackHeight = metrics.switchTrackHeight * scale;
    const qreal nominalThumbDiameter = metrics.switchThumbDiameter * scale;
    const qreal thumbHeight = std::min(trackHeight - metrics.borderWidth * 2.0,
                                       std::max(nominalThumbDiameter, trackHeight * 0.82));
    const qreal thumbWidth = std::max(trackWidth * 0.54, thumbHeight * 1.22);
    const QRectF trackRect(rect().center().x() - trackWidth * 0.5,
                           rect().center().y() - trackHeight * 0.5, trackWidth, trackHeight);

    QColor trackBase = isChecked() ? colors.accent : colors.controlFill;
    const QColor hoverColor = isChecked() ? colors.accentHovered : colors.controlFillHovered;
    const QColor pressColor = isChecked() ? colors.accentPressed : colors.controlFillPressed;
    trackBase = VkStylePainter::mix(trackBase, hoverColor, d->hoverProgress * 0.55);
    trackBase = VkStylePainter::mix(trackBase, pressColor, d->pressProgress);
    QColor border =
        isChecked() ? VkStylePainter::mix(colors.accent, colors.borderStrong, 0.22) : colors.border;
    if (!isEnabled()) {
        trackBase = isChecked()
                        ? VkStylePainter::mix(colors.controlFillDisabled, colors.accent, 0.28)
                        : colors.controlFillDisabled;
        border = VkStylePainter::multiplyAlpha(colors.border, 0.62);
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    VkStylePainter::drawRoundedPanel(painter, trackRect, trackHeight * 0.5, trackBase, border,
                                     metrics.borderWidth);

    const qreal thumbInset = std::max(metrics.borderWidth, (trackHeight - thumbHeight) * 0.5);
    const qreal travel = std::max<qreal>(0.0, trackWidth - thumbWidth - thumbInset * 2.0);
    qreal visualProgress = d->thumbProgress;
    if (layoutDirection() == Qt::RightToLeft) {
        visualProgress = 1.0 - visualProgress;
    }
    const qreal drawnThumbWidth = thumbWidth * (1.0 + 0.055 * d->pressProgress);
    const qreal drawnThumbHeight = thumbHeight * (1.0 - 0.055 * d->pressProgress);
    const QPointF thumbCenter(trackRect.left() + thumbInset + thumbWidth * 0.5 +
                                  travel * visualProgress,
                              trackRect.center().y());
    const QRectF thumbRect(thumbCenter.x() - drawnThumbWidth * 0.5,
                           thumbCenter.y() - drawnThumbHeight * 0.5, drawnThumbWidth,
                           drawnThumbHeight);

    QColor shadow = VkStylePainter::multiplyAlpha(colors.shadow, isEnabled() ? 0.24 : 0.10);
    painter.setPen(Qt::NoPen);
    painter.setBrush(shadow);
    painter.drawRoundedRect(thumbRect.translated(0.0, std::max<qreal>(0.5, scale)),
                            drawnThumbHeight * 0.5, drawnThumbHeight * 0.5);

    QColor thumbFill = colors.elevatedBackground;
    if (!isEnabled()) {
        thumbFill =
            VkStylePainter::mix(colors.elevatedBackground, colors.controlFillDisabled, 0.34);
    }
    painter.setBrush(thumbFill);
    painter.setPen(
        QPen(VkStylePainter::multiplyAlpha(colors.borderStrong, isEnabled() ? 0.28 : 0.14),
             metrics.borderWidth));
    const QRectF paintedThumb =
        thumbRect.adjusted(metrics.borderWidth * 0.5, metrics.borderWidth * 0.5,
                           -metrics.borderWidth * 0.5, -metrics.borderWidth * 0.5);
    painter.drawRoundedRect(paintedThumb, paintedThumb.height() * 0.5, paintedThumb.height() * 0.5);

    if (hasFocus() && d->keyboardFocusVisible && focusPolicy() != Qt::NoFocus) {
        VkStylePainter::drawFocusRing(painter,
                                      trackRect.adjusted(-metrics.spacing2, -metrics.spacing2,
                                                         metrics.spacing2, metrics.spacing2),
                                      trackHeight * 0.5 + metrics.spacing2,
                                      VkStylePainter::neutralFocusColor(colors.borderStrong),
                                      std::max<qreal>(1.0, metrics.borderWidth));
    }
}

bool VkSwitch::hitButton(const QPoint& position) const {
    return rect().contains(position);
}

bool VkSwitch::event(QEvent* event) {
    switch (event->type()) {
    case QEvent::Enter:
    case QEvent::HoverEnter:
        d->setHovered(true);
        break;
    case QEvent::Leave:
    case QEvent::HoverLeave:
        d->setHovered(false);
        d->setPressed(false);
        break;
    case QEvent::MouseButtonPress:
        d->keyboardFocusVisible = false;
        update();
        break;
    case QEvent::KeyPress:
        if (hasFocus()) {
            d->keyboardFocusVisible = true;
            update();
        }
        break;
    case QEvent::FocusIn:
        d->keyboardFocusVisible =
            VkStylePainter::showsKeyboardFocus(static_cast<QFocusEvent*>(event)->reason());
        update();
        break;
    case QEvent::FocusOut:
        d->keyboardFocusVisible = false;
        d->setPressed(false);
        break;
    default:
        break;
    }
    return QAbstractButton::event(event);
}

void VkSwitch::changeEvent(QEvent* event) {
    QAbstractButton::changeEvent(event);
    switch (event->type()) {
    case QEvent::EnabledChange:
        if (!isEnabled()) {
            d->setPressed(false);
        }
        update();
        break;
    case QEvent::StyleChange:
    case QEvent::FontChange:
    case QEvent::LayoutDirectionChange:
        updateGeometry();
        update();
        break;
    case QEvent::PaletteChange:
        update();
        break;
    default:
        break;
    }
}

} // namespace vkui
