// SPDX-License-Identifier: MIT

#include <QtWidgets/QAbstractItemView>
#include <vkui/widgets/VCombobox.h>

namespace {

bool isValidElideMode(const Qt::TextElideMode mode) noexcept {
    return mode == Qt::ElideLeft || mode == Qt::ElideRight || mode == Qt::ElideMiddle ||
           mode == Qt::ElideNone;
}
} // namespace

namespace vkui {

VCombobox::VCombobox(QWidget* parent) : QComboBox(parent) {
    QSizePolicy policy = sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Minimum);
    setSizePolicy(policy);
}

void VCombobox::setElideMode(const Qt::TextElideMode mode) {
    const Qt::TextElideMode resolved = isValidElideMode(mode) ? mode : Qt::ElideRight;
    if (elideMode_ == resolved) {
        return;
    }
    elideMode_ = resolved;
    update();
    if (auto* popupView = findChild<QAbstractItemView*>()) {
        popupView->viewport()->update();
    }
    emit elideModeChanged(elideMode_);
}

Qt::TextElideMode VCombobox::elideMode() const noexcept {
    return elideMode_;
}

} // namespace vkui
