// SPDX-License-Identifier: MIT

#include <QtWidgets/QComboBox>
#include <vkui/widgets/VkComboBox.h>

namespace {
constexpr auto elideModeProperty = "_vkui_combo_box_elide_mode";

bool isValidElideMode(const Qt::TextElideMode mode) {
    return mode == Qt::ElideLeft || mode == Qt::ElideRight || mode == Qt::ElideMiddle ||
           mode == Qt::ElideNone;
}
} // namespace

namespace vkui {

void setComboBoxElideMode(QComboBox& comboBox, const Qt::TextElideMode mode) {
    const Qt::TextElideMode resolved = isValidElideMode(mode) ? mode : Qt::ElideRight;
    if (comboBoxElideMode(comboBox) == resolved) {
        return;
    }
    comboBox.setProperty(elideModeProperty, static_cast<int>(resolved));
    comboBox.update();
}

Qt::TextElideMode comboBoxElideMode(const QComboBox& comboBox) {
    const QVariant value = comboBox.property(elideModeProperty);
    if (!value.isValid()) {
        return Qt::ElideRight;
    }
    const auto mode = static_cast<Qt::TextElideMode>(value.toInt());
    return isValidElideMode(mode) ? mode : Qt::ElideRight;
}

} // namespace vkui
