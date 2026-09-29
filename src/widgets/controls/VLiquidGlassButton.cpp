// SPDX-License-Identifier: MIT

#include <QtGui/QMoveEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QResizeEvent>
#include <QtWidgets/QStyle>
#include <QtWidgets/QStyleOptionButton>
#include <algorithm>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/controls/VLiquidGlassButton.h>

namespace vkui {

VLiquidGlassButton::VLiquidGlassButton(QWidget* parent) : QPushButton(parent) {
    initialize();
}

VLiquidGlassButton::VLiquidGlassButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent) {
    initialize();
}

void VLiquidGlassButton::initialize() {
    material_ = new VLiquidGlassSurface(this);
    material_->setGlassStyle(VLiquidGlassStyle::control());
    material_->setGeometry(rect());
    // The button composites the cached material before its label, so this child stays hidden.
    material_->hide();
    setAttribute(Qt::WA_Hover, true);

    connect(material_, &VLiquidGlassSurface::backdropChanged, this,
            [this](VLiquidGlassBackdrop* backdrop) {
                disconnect(backdropInvalidatedConnection_);
                if (backdrop) {
                    backdropInvalidatedConnection_ =
                        connect(backdrop, &VLiquidGlassBackdrop::invalidated, this,
                                qOverload<>(&VLiquidGlassButton::update));
                }
                update();
            });
    connect(material_, &VLiquidGlassSurface::glassStyleChanged, this,
            qOverload<>(&VLiquidGlassButton::update));
    connect(material_, &VLiquidGlassSurface::glassEnabledChanged, this,
            qOverload<>(&VLiquidGlassButton::update));
    auto* themeManager = VkThemeManager::instance();
    connect(themeManager, &VkThemeManager::themeChanged, this,
            [this](quint64, VkThemeChanges) { update(); });
    connect(themeManager, &VkThemeManager::liquidGlassEnabledChanged, this,
            qOverload<>(&VLiquidGlassButton::update));
    connect(themeManager, &VkThemeManager::liquidGlassTintLevelChanged, this,
            qOverload<>(&VLiquidGlassButton::update));
}

void VLiquidGlassButton::setBackdrop(VLiquidGlassBackdrop* backdrop) {
    material_->setBackdrop(backdrop);
}

VLiquidGlassBackdrop* VLiquidGlassButton::backdrop() const noexcept {
    return material_->backdrop();
}

void VLiquidGlassButton::setGlassStyle(const VLiquidGlassStyle& style) {
    material_->setGlassStyle(style);
}

VLiquidGlassStyle VLiquidGlassButton::glassStyle() const noexcept {
    return material_->glassStyle();
}

void VLiquidGlassButton::setGlassEnabled(const bool enabled) {
    material_->setGlassEnabled(enabled);
}

bool VLiquidGlassButton::isGlassEnabled() const noexcept {
    return material_->isGlassEnabled();
}

void VLiquidGlassButton::paintEvent(QPaintEvent* event) {
    if (!VkThemeManager::instance()->liquidGlassEnabled() || !isGlassEnabled() || !backdrop() ||
        !backdrop()->sourceWidget()) {
        QPushButton::paintEvent(event);
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    material_->paintMaterial(painter);

    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal maximumRadius = std::min(bounds.width(), bounds.height()) * 0.5;
    const VLiquidGlassStyle materialStyle = glassStyle();
    const qreal radius = materialStyle.cornerRadius < 0.0
                             ? maximumRadius
                             : std::clamp(materialStyle.cornerRadius, 0.0, maximumRadius);
    QPainterPath outline;
    outline.addRoundedRect(bounds, radius, radius);
    if (isDown() || underMouse()) {
        const bool dark =
            VkThemeManager::instance()->theme().effectiveAppearance() == VkAppearance::Dark;
        const int alpha = isDown() ? 34 : 16;
        painter.fillPath(outline, dark ? QColor(255, 255, 255, alpha) : QColor(0, 0, 0, alpha));
    }
    if (hasFocus() && focusPolicy() != Qt::NoFocus) {
        QColor focus = VkThemeManager::instance()->theme().colors().borderStrong;
        focus.setAlpha(160);
        painter.setPen(QPen(focus, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(outline);
    }

    QStyleOptionButton option;
    initStyleOption(&option);
    option.features &= ~QStyleOptionButton::DefaultButton;
    style()->drawControl(QStyle::CE_PushButtonLabel, &option, &painter, this);
}

void VLiquidGlassButton::moveEvent(QMoveEvent* event) {
    QPushButton::moveEvent(event);
    if (material_ && backdrop()) {
        backdrop()->invalidate();
    }
}

void VLiquidGlassButton::resizeEvent(QResizeEvent* event) {
    QPushButton::resizeEvent(event);
    material_->setGeometry(rect());
}

} // namespace vkui
