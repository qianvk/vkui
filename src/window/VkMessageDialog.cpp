// SPDX-License-Identifier: MIT

#include <QAbstractButton>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <vkui/core/VkIcon.h>
#include <vkui/window/VkMessageDialog.h>

namespace vkui {
namespace {

QIcon messageIcon(VkMessageDialog::Icon type) {
    switch (type) {
    case VkMessageDialog::Icon::Information:
        return icon(VkSymbol::Information, VkIconRole::Accent);
    case VkMessageDialog::Icon::Warning:
        return icon(VkSymbol::Warning, VkIconRole::Accent);
    case VkMessageDialog::Icon::Critical:
        return icon(VkSymbol::Warning, VkIconRole::Destructive);
    }
    return {};
}

} // namespace

VkMessageDialog::VkMessageDialog(Icon type, const QString& title, const QString& text,
                                 QDialogButtonBox::StandardButtons buttons, QWidget* parent)
    : VkFramelessDialog(title, parent), buttons_(new QDialogButtonBox(buttons, this)) {
    setObjectName(QStringLiteral("VkMessageDialog"));
    setMinimumWidth(380);
    setMaximumWidth(600);

    auto* messageRow = new QHBoxLayout;
    messageRow->setContentsMargins(2, 4, 2, 4);
    messageRow->setSpacing(14);

    auto* iconLabel = new QLabel(this);
    iconLabel->setObjectName(QStringLiteral("VkMessageDialogIcon"));
    iconLabel->setFixedSize(36, 36);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setPixmap(messageIcon(type).pixmap(32, 32));
    messageRow->addWidget(iconLabel, 0, Qt::AlignTop);

    auto* messageLabel = new QLabel(text, this);
    messageLabel->setObjectName(QStringLiteral("VkMessageDialogText"));
    messageLabel->setWordWrap(true);
    messageLabel->setTextFormat(Qt::PlainText);
    messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    messageLabel->setMinimumWidth(280);
    messageLabel->setMaximumWidth(500);
    messageRow->addWidget(messageLabel, 1, Qt::AlignVCenter);
    contentLayout()->addLayout(messageRow);
    contentLayout()->addWidget(buttons_);

    connect(buttons_, &QDialogButtonBox::clicked, this, [this](QAbstractButton* button) {
        clickedButton_ = button;
        const auto role = buttons_->buttonRole(button);
        if (role == QDialogButtonBox::RejectRole || role == QDialogButtonBox::NoRole) {
            VkFramelessDialog::reject();
        } else {
            VkFramelessDialog::accept();
        }
    });

    if (auto* cancel = buttons_->button(QDialogButtonBox::Cancel)) {
        setEscapeButton(cancel);
    } else if (auto* no = buttons_->button(QDialogButtonBox::No)) {
        setEscapeButton(no);
    }
}

QPushButton* VkMessageDialog::addButton(const QString& text, QDialogButtonBox::ButtonRole role) {
    return buttons_->addButton(text, role);
}

QPushButton* VkMessageDialog::button(QDialogButtonBox::StandardButton button) const {
    return buttons_->button(button);
}

void VkMessageDialog::setDefaultButton(QAbstractButton* button) {
    auto* defaultButton = qobject_cast<QPushButton*>(button);
    if (defaultButton == nullptr || !buttons_->buttons().contains(defaultButton)) {
        return;
    }
    defaultButton_ = defaultButton;

    // Keep Enter deterministic even after focus moves to a destructive action.
    // Space still activates a focused button, preserving keyboard accessibility.
    for (QAbstractButton* candidate : buttons_->buttons()) {
        if (auto* pushButton = qobject_cast<QPushButton*>(candidate)) {
            const bool isDefault = defaultButtonIndicatorVisible_ && pushButton == defaultButton;
            pushButton->setAutoDefault(isDefault);
            pushButton->setDefault(isDefault);
        }
    }
    defaultButton->setFocus(Qt::OtherFocusReason);
}

void VkMessageDialog::setDefaultButton(QDialogButtonBox::StandardButton button) {
    setDefaultButton(buttons_->button(button));
}

bool VkMessageDialog::defaultButtonIndicatorVisible() const noexcept {
    return defaultButtonIndicatorVisible_;
}

void VkMessageDialog::setDefaultButtonIndicatorVisible(const bool visible) {
    if (defaultButtonIndicatorVisible_ == visible) {
        return;
    }
    defaultButtonIndicatorVisible_ = visible;
    for (QAbstractButton* candidate : buttons_->buttons()) {
        if (auto* pushButton = qobject_cast<QPushButton*>(candidate)) {
            const bool isDefault = visible && pushButton == defaultButton_;
            pushButton->setAutoDefault(isDefault);
            pushButton->setDefault(isDefault);
        }
    }
}

void VkMessageDialog::setEscapeButton(QAbstractButton* button) {
    if (button == nullptr || buttons_->buttons().contains(button)) {
        escapeButton_ = button;
    }
}

void VkMessageDialog::setEscapeButton(QDialogButtonBox::StandardButton button) {
    setEscapeButton(buttons_->button(button));
}

QAbstractButton* VkMessageDialog::clickedButton() const {
    return clickedButton_.data();
}

QDialogButtonBox::StandardButton VkMessageDialog::clickedStandardButton() const {
    return clickedButton_ != nullptr ? buttons_->standardButton(clickedButton_.data())
                                     : QDialogButtonBox::NoButton;
}

QDialogButtonBox::StandardButton
VkMessageDialog::information(QWidget* parent, const QString& title, const QString& text,
                             QDialogButtonBox::StandardButtons buttons,
                             QDialogButtonBox::StandardButton defaultButton) {
    return run(Icon::Information, parent, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton
VkMessageDialog::warning(QWidget* parent, const QString& title, const QString& text,
                         QDialogButtonBox::StandardButtons buttons,
                         QDialogButtonBox::StandardButton defaultButton) {
    return run(Icon::Warning, parent, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton
VkMessageDialog::critical(QWidget* parent, const QString& title, const QString& text,
                          QDialogButtonBox::StandardButtons buttons,
                          QDialogButtonBox::StandardButton defaultButton) {
    return run(Icon::Critical, parent, title, text, buttons, defaultButton);
}

bool VkMessageDialog::confirmDestructive(QWidget* parent, const QString& title, const QString& text,
                                         const QString& confirmText) {
    VkMessageDialog prompt(Icon::Warning, title, text, QDialogButtonBox::Cancel, parent);
    QAbstractButton* confirmButton = prompt.addButton(
        confirmText.isEmpty() ? QCoreApplication::translate("VkMessageDialog", "Delete")
                              : confirmText,
        QDialogButtonBox::DestructiveRole);
    QPushButton* cancelButton = prompt.button(QDialogButtonBox::Cancel);
    prompt.setDefaultButton(cancelButton);
    prompt.setEscapeButton(cancelButton);
    prompt.exec();
    return prompt.clickedButton() == confirmButton;
}

void VkMessageDialog::reject() {
    if (clickedButton_ == nullptr) {
        clickedButton_ = escapeButton_;
    }
    VkFramelessDialog::reject();
}

void VkMessageDialog::keyPressEvent(QKeyEvent* event) {
    if (event != nullptr && defaultButton_ != nullptr && defaultButton_->isEnabled() &&
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier)) {
        defaultButton_->click();
        event->accept();
        return;
    }
    VkFramelessDialog::keyPressEvent(event);
}

QDialogButtonBox::StandardButton
VkMessageDialog::run(Icon icon, QWidget* parent, const QString& title, const QString& text,
                     QDialogButtonBox::StandardButtons buttons,
                     QDialogButtonBox::StandardButton defaultButton) {
    VkMessageDialog dialog(icon, title, text, buttons, parent);
    dialog.setDefaultButton(defaultButton);
    dialog.exec();
    return dialog.clickedStandardButton();
}

} // namespace vkui
