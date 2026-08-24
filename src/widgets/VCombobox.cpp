// SPDX-License-Identifier: MIT

#include <QtCore/QItemSelectionModel>
#include <QtCore/QSignalBlocker>
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

void VCombobox::showPopup() {
    // Qt positions menu-style combo popups from the view's current index. Hovering mutates that
    // index without changing QComboBox::currentIndex(), so restore the committed item first.
    if (count() > 0) {
        synchronizePopupCurrentIndex();
    }
    QComboBox::showPopup();
}

void VCombobox::synchronizePopupCurrentIndex() {
    QAbstractItemView* popupView = view();
    QItemSelectionModel* selection = popupView ? popupView->selectionModel() : nullptr;
    if (!selection || !model()) {
        return;
    }

    const QModelIndex committedIndex =
        model()->index(currentIndex(), modelColumn(), rootModelIndex());
    if (selection->currentIndex() == committedIndex) {
        return;
    }

    const QSignalBlocker viewBlocker(popupView);
    const QSignalBlocker selectionBlocker(selection);
    selection->setCurrentIndex(committedIndex, QItemSelectionModel::NoUpdate);
}

} // namespace vkui
