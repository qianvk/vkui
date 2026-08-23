// SPDX-License-Identifier: MIT

#include <QtCore/QObject>
#include <QtCore/QVariant>
#include <QtWidgets/QWidget>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeChange.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VTextStyle.h>

namespace {

constexpr auto textStyleProperty = "_vkui_textStyle";
constexpr auto textStyleBindingProperty = "_vkui_textStyleBinding";

bool isValidTextStyle(const int value) noexcept {
    return value >= static_cast<int>(vkui::VTextStyle::Caption) &&
           value <= static_cast<int>(vkui::VTextStyle::LargeTitle);
}

class VTextStyleBinding final : public QObject {
  public:
    explicit VTextStyleBinding(QWidget& widget) : QObject(&widget), widget_(&widget) {
        connect(vkui::VkThemeManager::instance(), &vkui::VkThemeManager::themeChanged, this,
                [this](quint64, const vkui::VkThemeChanges changes) {
                    if (changes.testFlag(vkui::VkThemeChange::Typography)) {
                        apply();
                    }
                });
    }

    void setStyle(const vkui::VTextStyle style) {
        style_ = style;
        apply();
    }

  private:
    void apply() {
        if (widget_) {
            widget_->setFont(vkui::textStyleFont(style_));
        }
    }

    QWidget* widget_ = nullptr;
    vkui::VTextStyle style_ = vkui::VTextStyle::Body;
};

VTextStyleBinding* binding(const QWidget& widget) {
    QObject* object = widget.property(textStyleBindingProperty).value<QObject*>();
    return static_cast<VTextStyleBinding*>(object);
}

} // namespace

namespace vkui {

QFont textStyleFont(const VTextStyle style) {
    const VkTypographyTokens& typography = VkThemeManager::instance()->theme().typography();
    switch (style) {
    case VTextStyle::Caption:
        return typography.caption;
    case VTextStyle::BodyEmphasized:
        return typography.bodyEmphasized;
    case VTextStyle::Headline:
        return typography.headline;
    case VTextStyle::Title:
        return typography.title;
    case VTextStyle::LargeTitle:
        return typography.largeTitle;
    case VTextStyle::Body:
    default:
        return typography.body;
    }
}

std::optional<VTextStyle> textStyle(const QWidget& widget) noexcept {
    const QVariant value = widget.property(textStyleProperty);
    if (!value.isValid() || !isValidTextStyle(value.toInt())) {
        return std::nullopt;
    }
    return static_cast<VTextStyle>(value.toInt());
}

void setTextStyle(QWidget& widget, const VTextStyle style) {
    if (!isValidTextStyle(static_cast<int>(style))) {
        qWarning("setTextStyle received an invalid VTextStyle value");
        return;
    }
    VTextStyleBinding* currentBinding = binding(widget);
    if (!currentBinding) {
        currentBinding = new VTextStyleBinding(widget);
        widget.setProperty(textStyleBindingProperty,
                           QVariant::fromValue(static_cast<QObject*>(currentBinding)));
    }
    widget.setProperty(textStyleProperty, static_cast<int>(style));
    currentBinding->setStyle(style);
}

void resetTextStyle(QWidget& widget) {
    VTextStyleBinding* currentBinding = binding(widget);
    widget.setProperty(textStyleProperty, QVariant{});
    widget.setProperty(textStyleBindingProperty, QVariant{});
    delete currentBinding;
    widget.setFont(QFont{});
}

} // namespace vkui
