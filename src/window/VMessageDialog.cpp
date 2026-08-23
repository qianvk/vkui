// SPDX-License-Identifier: MIT

#include <QAbstractButton>
#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <optional>
#include <vkui/core/VkIcon.h>
#include <vkui/window/VMessageDialog.h>
#include <vkui/window/VWindowAgent.h>

namespace vkui {
namespace {

constexpr int kTitleBarHeight = 44;

QIcon messageIcon(const VMessageDialog::Icon type) {
    switch (type) {
    case VMessageDialog::Icon::Information:
        return icon(VkSymbol::Information, VkIconRole::Accent);
    case VMessageDialog::Icon::Warning:
        return icon(VkSymbol::Warning, VkIconRole::Accent);
    case VMessageDialog::Icon::Critical:
        return icon(VkSymbol::Warning, VkIconRole::Destructive);
    }
    return {};
}

bool isValidButtonRole(const QDialogButtonBox::ButtonRole role) {
    return role >= QDialogButtonBox::AcceptRole && role < QDialogButtonBox::NRoles;
}

} // namespace

class VMessageDialogPrivate final {
  public:
    explicit VMessageDialogPrivate(const VMessageDialog::Icon type) : iconType(type) {}

    VMessageDialog::Icon iconType;
    QFrame* surface = nullptr;
    QWidget* titleBar = nullptr;
    QLabel* titleLabel = nullptr;
    QLabel* iconLabel = nullptr;
    QLabel* messageLabel = nullptr;
    QDialogButtonBox* buttonBox = nullptr;
    QPointer<QAbstractButton> clickedButton;
    QPointer<QPushButton> defaultButton;
    QPointer<QAbstractButton> escapeButton;
    bool defaultButtonIndicatorVisible = true;
    bool escapeButtonExplicitlySet = false;
    // Declaring the agent last guarantees that it detaches first.
    std::optional<VWindowAgent> windowAgent;
};

VMessageDialog::VMessageDialog(const Icon type, const QString& title, const QString& text,
                               const QDialogButtonBox::StandardButtons buttons, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint),
      d_(std::make_unique<VMessageDialogPrivate>(type)) {
    setObjectName(QStringLiteral("VMessageDialog"));
    setWindowTitle(title);
    // Parent-owned prompts block only their host window. Standalone prompts
    // must block the application because they have no narrower modal scope.
    setWindowModality(parent != nullptr ? Qt::WindowModal : Qt::ApplicationModal);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    setMinimumWidth(380);
    setMaximumWidth(600);
    setSizeGripEnabled(false);

    buildUi(type, text, buttons);
    configureWindowChrome();

    refreshAutomaticEscapeButton();
    refreshDefaultButtonState();
}

VMessageDialog::~VMessageDialog() = default;

void VMessageDialog::addButton(QAbstractButton* button,
                               const QDialogButtonBox::ButtonRole role) {
    if (button == nullptr || !isValidButtonRole(role)) {
        return;
    }
    d_->buttonBox->addButton(button, role);
    refreshAutomaticEscapeButton();
    refreshDefaultButtonState();
}

QPushButton* VMessageDialog::addButton(const QDialogButtonBox::StandardButton button) {
    if (button == QDialogButtonBox::NoButton) {
        return nullptr;
    }
    auto* added = d_->buttonBox->addButton(button);
    refreshAutomaticEscapeButton();
    refreshDefaultButtonState();
    return added;
}

QPushButton* VMessageDialog::addButton(const QString& text,
                                       const QDialogButtonBox::ButtonRole role) {
    if (!isValidButtonRole(role)) {
        return nullptr;
    }
    auto* added = d_->buttonBox->addButton(text, role);
    refreshAutomaticEscapeButton();
    refreshDefaultButtonState();
    return added;
}

void VMessageDialog::removeButton(QAbstractButton* button) {
    if (!ownsButton(button)) {
        return;
    }
    if (d_->clickedButton == button) {
        d_->clickedButton.clear();
    }
    if (d_->defaultButton == button) {
        d_->defaultButton.clear();
    }
    if (d_->escapeButton == button) {
        d_->escapeButton.clear();
    }
    d_->buttonBox->removeButton(button);
    refreshAutomaticEscapeButton();
    refreshDefaultButtonState();
}

void VMessageDialog::clearButtons() {
    d_->clickedButton.clear();
    d_->defaultButton.clear();
    d_->escapeButton.clear();
    d_->buttonBox->clear();
}

QList<QAbstractButton*> VMessageDialog::buttons() const {
    return d_->buttonBox->buttons();
}

QPushButton* VMessageDialog::button(const QDialogButtonBox::StandardButton button) const {
    return d_->buttonBox->button(button);
}

QDialogButtonBox::ButtonRole VMessageDialog::buttonRole(QAbstractButton* button) const {
    return d_->buttonBox->buttonRole(button);
}

QDialogButtonBox::StandardButton VMessageDialog::standardButton(QAbstractButton* button) const {
    return d_->buttonBox->standardButton(button);
}

bool VMessageDialog::setButtonRole(QAbstractButton* button,
                                   const QDialogButtonBox::ButtonRole role) {
    if (!ownsButton(button) || !isValidButtonRole(role)) {
        return false;
    }
    d_->buttonBox->addButton(button, role);
    refreshAutomaticEscapeButton();
    refreshDefaultButtonState();
    return true;
}

void VMessageDialog::setDefaultButton(QAbstractButton* button) {
    if (button == nullptr) {
        d_->defaultButton.clear();
        refreshDefaultButtonState();
        return;
    }
    auto* pushButton = qobject_cast<QPushButton*>(button);
    if (pushButton == nullptr || !ownsButton(pushButton)) {
        return;
    }
    d_->defaultButton = pushButton;
    refreshDefaultButtonState();
    pushButton->setFocus(Qt::OtherFocusReason);
}

void VMessageDialog::setDefaultButton(const QDialogButtonBox::StandardButton button) {
    setDefaultButton(d_->buttonBox->button(button));
}

QPushButton* VMessageDialog::defaultButton() const {
    return d_->defaultButton.data();
}

bool VMessageDialog::defaultButtonIndicatorVisible() const noexcept {
    return d_->defaultButtonIndicatorVisible;
}

void VMessageDialog::setDefaultButtonIndicatorVisible(const bool visible) {
    if (d_->defaultButtonIndicatorVisible == visible) {
        return;
    }
    d_->defaultButtonIndicatorVisible = visible;
    refreshDefaultButtonState();
}

void VMessageDialog::setEscapeButton(QAbstractButton* button) {
    if (button == nullptr) {
        d_->escapeButton.clear();
        d_->escapeButtonExplicitlySet = true;
    } else if (ownsButton(button)) {
        d_->escapeButton = button;
        d_->escapeButtonExplicitlySet = true;
    }
}

void VMessageDialog::setEscapeButton(const QDialogButtonBox::StandardButton button) {
    setEscapeButton(d_->buttonBox->button(button));
}

QAbstractButton* VMessageDialog::escapeButton() const {
    return d_->escapeButton.data();
}

QAbstractButton* VMessageDialog::clickedButton() const {
    return d_->clickedButton.data();
}

QDialogButtonBox::StandardButton VMessageDialog::clickedStandardButton() const {
    return d_->clickedButton != nullptr ? d_->buttonBox->standardButton(d_->clickedButton.data())
                                        : QDialogButtonBox::NoButton;
}

QDialogButtonBox::StandardButton
VMessageDialog::information(QWidget* parent, const QString& title, const QString& text,
                            const QDialogButtonBox::StandardButtons buttons,
                            const QDialogButtonBox::StandardButton defaultButton) {
    return run(Icon::Information, parent, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton
VMessageDialog::warning(QWidget* parent, const QString& title, const QString& text,
                        const QDialogButtonBox::StandardButtons buttons,
                        const QDialogButtonBox::StandardButton defaultButton) {
    return run(Icon::Warning, parent, title, text, buttons, defaultButton);
}

QDialogButtonBox::StandardButton
VMessageDialog::critical(QWidget* parent, const QString& title, const QString& text,
                         const QDialogButtonBox::StandardButtons buttons,
                         const QDialogButtonBox::StandardButton defaultButton) {
    return run(Icon::Critical, parent, title, text, buttons, defaultButton);
}

bool VMessageDialog::confirmDestructive(QWidget* parent, const QString& title, const QString& text,
                                        const QString& confirmText) {
    VMessageDialog prompt(Icon::Warning, title, text, QDialogButtonBox::Cancel, parent);
    QAbstractButton* confirmButton = prompt.addButton(
        confirmText.isEmpty() ? QCoreApplication::translate("VMessageDialog", "Delete")
                              : confirmText,
        QDialogButtonBox::DestructiveRole);
    QPushButton* cancelButton = prompt.button(QDialogButtonBox::Cancel);
    prompt.setDefaultButton(cancelButton);
    prompt.setEscapeButton(cancelButton);
    prompt.exec();
    return prompt.clickedButton() == confirmButton;
}

void VMessageDialog::reject() {
    if (d_->clickedButton == nullptr) {
        d_->clickedButton = d_->escapeButton;
    }
    QDialog::reject();
}

void VMessageDialog::changeEvent(QEvent* event) {
    if (event != nullptr) {
        if (event->type() == QEvent::WindowTitleChange || event->type() == QEvent::LanguageChange) {
            refreshTitle();
        }
        if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
            refreshIcon();
        }
    }
    QDialog::changeEvent(event);
}

void VMessageDialog::keyPressEvent(QKeyEvent* event) {
    if (event == nullptr) {
        return;
    }
    const bool unmodified =
        event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::KeypadModifier;
    if (unmodified && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        if (d_->defaultButton != nullptr && d_->defaultButton->isEnabled()) {
            d_->defaultButton->click();
        }
        event->accept();
        return;
    }
    if (unmodified && event->key() == Qt::Key_Escape) {
        if (d_->escapeButton != nullptr && d_->escapeButton->isEnabled()) {
            d_->escapeButton->click();
        }
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

QDialogButtonBox::StandardButton
VMessageDialog::run(const Icon icon, QWidget* parent, const QString& title, const QString& text,
                    const QDialogButtonBox::StandardButtons buttons,
                    const QDialogButtonBox::StandardButton defaultButton) {
    VMessageDialog dialog(icon, title, text, buttons, parent);
    dialog.setDefaultButton(defaultButton);
    dialog.exec();
    return dialog.clickedStandardButton();
}

void VMessageDialog::buildUi(const Icon type, const QString& text,
                             const QDialogButtonBox::StandardButtons buttons) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    d_->surface = new QFrame(this);
    d_->surface->setObjectName(QStringLiteral("VMessageDialogSurface"));
    d_->surface->setFrameShape(QFrame::NoFrame);
    d_->surface->setBackgroundRole(QPalette::Window);
    d_->surface->setAutoFillBackground(true);
    root->addWidget(d_->surface, 1);

    auto* surfaceLayout = new QVBoxLayout(d_->surface);
    surfaceLayout->setContentsMargins(0, 0, 0, 0);
    surfaceLayout->setSpacing(0);

    d_->titleBar = new QWidget(d_->surface);
    d_->titleBar->setObjectName(QStringLiteral("VMessageDialogTitleBar"));
    d_->titleBar->setAttribute(Qt::WA_StyledBackground, true);
    d_->titleBar->setFixedHeight(kTitleBarHeight);
    auto* titleLayout = new QHBoxLayout(d_->titleBar);
    titleLayout->setContentsMargins(18, 0, 18, 0);

    d_->titleLabel = new QLabel(windowTitle(), d_->titleBar);
    d_->titleLabel->setObjectName(QStringLiteral("VMessageDialogTitleLabel"));
    QFont titleFont = d_->titleLabel->font();
    titleFont.setWeight(QFont::DemiBold);
    d_->titleLabel->setFont(titleFont);
    titleLayout->addWidget(d_->titleLabel, 1, Qt::AlignVCenter);
    surfaceLayout->addWidget(d_->titleBar);

    auto* content = new QWidget(d_->surface);
    content->setObjectName(QStringLiteral("VMessageDialogContent"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(18, 0, 18, 18);
    contentLayout->setSpacing(14);

    auto* messageRow = new QHBoxLayout;
    messageRow->setContentsMargins(2, 4, 2, 4);
    messageRow->setSpacing(14);

    d_->iconType = type;
    d_->iconLabel = new QLabel(content);
    d_->iconLabel->setObjectName(QStringLiteral("VMessageDialogIcon"));
    d_->iconLabel->setFixedSize(36, 36);
    d_->iconLabel->setAlignment(Qt::AlignCenter);
    messageRow->addWidget(d_->iconLabel, 0, Qt::AlignTop);

    d_->messageLabel = new QLabel(text, content);
    d_->messageLabel->setObjectName(QStringLiteral("VMessageDialogText"));
    d_->messageLabel->setWordWrap(true);
    d_->messageLabel->setTextFormat(Qt::PlainText);
    d_->messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    d_->messageLabel->setMinimumWidth(280);
    d_->messageLabel->setMaximumWidth(500);
    messageRow->addWidget(d_->messageLabel, 1, Qt::AlignVCenter);
    contentLayout->addLayout(messageRow);

    d_->buttonBox = new QDialogButtonBox(buttons, content);
    d_->buttonBox->setObjectName(QStringLiteral("VMessageDialogButtonBox"));
    contentLayout->addWidget(d_->buttonBox);
    surfaceLayout->addWidget(content, 1);

    connect(d_->buttonBox, &QDialogButtonBox::clicked, this,
            &VMessageDialog::handleButtonClick);
    refreshIcon();
}

void VMessageDialog::configureWindowChrome() {
    d_->windowAgent.emplace(*this);
    d_->windowAgent->setSystemButtons(VSystemButtons{});
    d_->windowAgent->setResizable(false);
    const bool titleBarAdded = d_->windowAgent->addTitleBar(d_->titleBar);
    Q_ASSERT(titleBarAdded);
    Q_UNUSED(titleBarAdded);
}

void VMessageDialog::refreshAutomaticEscapeButton() {
    if (d_->escapeButtonExplicitlySet) {
        return;
    }

    if (auto* cancel = button(QDialogButtonBox::Cancel)) {
        d_->escapeButton = cancel;
        return;
    }

    const QList<QAbstractButton*> currentButtons = d_->buttonBox->buttons();
    if (currentButtons.size() == 1) {
        d_->escapeButton = currentButtons.front();
        return;
    }

    QAbstractButton* rejectCandidate = nullptr;
    for (QAbstractButton* candidate : currentButtons) {
        const auto role = buttonRole(candidate);
        if (role != QDialogButtonBox::RejectRole && role != QDialogButtonBox::NoRole) {
            continue;
        }
        if (rejectCandidate != nullptr) {
            d_->escapeButton.clear();
            return;
        }
        rejectCandidate = candidate;
    }
    d_->escapeButton = rejectCandidate;
}

void VMessageDialog::refreshDefaultButtonState() {
    for (QAbstractButton* candidate : d_->buttonBox->buttons()) {
        if (auto* pushButton = qobject_cast<QPushButton*>(candidate)) {
            const bool paintedDefault =
                d_->defaultButtonIndicatorVisible && pushButton == d_->defaultButton;
            pushButton->setAutoDefault(paintedDefault);
            pushButton->setDefault(paintedDefault);
        }
    }
}

void VMessageDialog::refreshIcon() {
    if (d_->iconLabel != nullptr) {
        d_->iconLabel->setPixmap(messageIcon(d_->iconType).pixmap(32, 32));
    }
}

void VMessageDialog::refreshTitle() {
    if (d_->titleLabel != nullptr) {
        d_->titleLabel->setText(windowTitle());
    }
}

void VMessageDialog::handleButtonClick(QAbstractButton* button) {
    const auto role = buttonRole(button);
    if (role == QDialogButtonBox::InvalidRole) {
        return;
    }

    d_->clickedButton = button;
    emit buttonClicked(button);
    if (role == QDialogButtonBox::HelpRole) {
        return;
    }
    if (role == QDialogButtonBox::RejectRole || role == QDialogButtonBox::NoRole) {
        QDialog::reject();
    } else {
        QDialog::accept();
    }
}

bool VMessageDialog::ownsButton(const QAbstractButton* button) const {
    return button != nullptr &&
           d_->buttonBox->buttonRole(const_cast<QAbstractButton*>(button)) !=
               QDialogButtonBox::InvalidRole;
}

} // namespace vkui
