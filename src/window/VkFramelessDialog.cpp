// SPDX-License-Identifier: MIT

#include <QCursor>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QtMath>
#include <vkui/core/VkIcon.h>
#include <vkui/window/VkFramelessDialog.h>
#include <vkui/window/VkWindowAgent.h>

namespace vkui {
namespace {

constexpr int kTitleBarHeight = 44;
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
constexpr int kMacCloseButtonReserve = 24;
constexpr QPoint kMacCloseButtonPosition{18, 15};
#else
constexpr int kSystemButtonReserve = 56;
#endif

} // namespace

VkFramelessDialog::VkFramelessDialog(const QString& title, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                          Qt::WindowCloseButtonHint),
      windowAgent_(new VkWindowAgent(this)) {
    setObjectName(QStringLiteral("VkFramelessDialog"));
    setWindowTitle(title);
    setWindowModality(parent != nullptr ? Qt::WindowModal : Qt::ApplicationModal);
    setAttribute(Qt::WA_DontCreateNativeAncestors);
    buildUi();
    installWindowChrome();
}

VkFramelessDialog::~VkFramelessDialog() = default;

bool VkFramelessDialog::isResizable() const {
    return resizable_;
}

void VkFramelessDialog::setResizable(bool resizable) {
    if (resizable_ == resizable) {
        return;
    }
    resizable_ = resizable;
    windowAgent_->setResizable(resizable);
}

VkFramelessDialog::CloseButtonPlacement VkFramelessDialog::closeButtonPlacement() const noexcept {
    return closeButtonPlacement_;
}

void VkFramelessDialog::setCloseButtonPlacement(const CloseButtonPlacement placement) {
    if (closeButtonPlacement_ == placement) {
        return;
    }
    closeButtonPlacement_ = placement;
    refreshCloseButtonPlacement();
}

QWidget* VkFramelessDialog::titleBar() const {
    return titleBar_;
}

QWidget* VkFramelessDialog::contentWidget() const {
    return content_;
}

QVBoxLayout* VkFramelessDialog::contentLayout() const {
    return contentLayout_;
}

VkWindowAgent* VkFramelessDialog::windowAgent() const {
    return windowAgent_;
}

void VkFramelessDialog::positionForHost(const QWidget* host, QSizeF fraction) {
    const auto normalizedFraction = [](const qreal value) {
        return qIsFinite(value) ? qBound(0.1, value, 1.0) : 0.8;
    };
    fraction.setWidth(normalizedFraction(fraction.width()));
    fraction.setHeight(normalizedFraction(fraction.height()));

    QRect available;
    if (host != nullptr) {
        available = host->frameGeometry();
        if (!host->isWindow() && host->parentWidget() != nullptr) {
            available.moveTopLeft(host->parentWidget()->mapToGlobal(available.topLeft()));
        }
    } else if (const QScreen* pointerScreen = QGuiApplication::screenAt(QCursor::pos())) {
        available = pointerScreen->availableGeometry();
    } else if (const QScreen* primaryScreen = QGuiApplication::primaryScreen()) {
        available = primaryScreen->availableGeometry();
    }
    if (!available.isValid()) {
        return;
    }

    const QSize target(qRound(available.width() * fraction.width()),
                       qRound(available.height() * fraction.height()));
    resize(target.expandedTo(minimumSize()).boundedTo(maximumSize()));
    move(available.center() - rect().center());
}

void VkFramelessDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowTitleChange || event->type() == QEvent::LanguageChange) {
        refreshTitle();
    }
    QDialog::changeEvent(event);
}

void VkFramelessDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    surface_ = new QFrame(this);
    surface_->setObjectName(QStringLiteral("VkFramelessDialogSurface"));
    surface_->setFrameShape(QFrame::NoFrame);
    surface_->setStyleSheet(
        QStringLiteral("QFrame#VkFramelessDialogSurface{background:palette(window);border:none;}"));
    root->addWidget(surface_, 1);

    auto* surfaceLayout = new QVBoxLayout(surface_);
    surfaceLayout->setContentsMargins(0, 0, 0, 0);
    surfaceLayout->setSpacing(0);

    titleBar_ = new QWidget(surface_);
    titleBar_->setObjectName(QStringLiteral("VkFramelessDialogTitleBar"));
    titleBar_->setAttribute(Qt::WA_StyledBackground, true);
    titleBar_->setFixedHeight(kTitleBarHeight);
    auto* titleLayout = new QHBoxLayout(titleBar_);
    titleLayout->setContentsMargins(18, 0, 10, 0);
    titleLayout->setSpacing(8);

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    nativeButtonReserve_ = new QWidget(titleBar_);
    nativeButtonReserve_->setFixedWidth(kMacCloseButtonReserve);
    titleLayout->addWidget(nativeButtonReserve_);
#endif

    titleLabel_ = new QLabel(windowTitle(), titleBar_);
    titleLabel_->setObjectName(QStringLiteral("VkFramelessDialogTitleLabel"));
    QFont titleFont = titleLabel_->font();
    titleFont.setWeight(QFont::DemiBold);
    titleLabel_->setFont(titleFont);
    titleLayout->addWidget(titleLabel_, 1, Qt::AlignVCenter);

    fallbackCloseButton_ = new QToolButton(titleBar_);
    fallbackCloseButton_->setObjectName(QStringLiteral("VkFramelessDialogCloseButton"));
    fallbackCloseButton_->setAutoRaise(true);
    fallbackCloseButton_->setFocusPolicy(Qt::NoFocus);
    fallbackCloseButton_->setIcon(icon(VkSymbol::Close));
    fallbackCloseButton_->setIconSize(QSize(16, 16));
    fallbackCloseButton_->setToolTip(tr("Close"));
    fallbackCloseButton_->setFixedSize(36, 32);
    connect(fallbackCloseButton_, &QToolButton::clicked, this, &QDialog::reject);
    titleLayout->addWidget(fallbackCloseButton_, 0, Qt::AlignVCenter);

#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
    nativeButtonReserve_ = new QWidget(titleBar_);
    nativeButtonReserve_->setFixedWidth(kSystemButtonReserve);
    titleLayout->addWidget(nativeButtonReserve_);
#endif
    surfaceLayout->addWidget(titleBar_);

    content_ = new QWidget(surface_);
    content_->setObjectName(QStringLiteral("VkFramelessDialogContent"));
    contentLayout_ = new QVBoxLayout(content_);
    contentLayout_->setContentsMargins(18, 0, 18, 18);
    contentLayout_->setSpacing(14);
    surfaceLayout->addWidget(content_, 1);
}

void VkFramelessDialog::installWindowChrome() {
    const bool setup = windowAgent_->setup(this);
    if (setup) {
        windowAgent_->setResizable(resizable_);
        platformCloseAvailable_ = windowAgent_->installSystemButtons();
        const bool titleBarAdded = windowAgent_->addTitleBar(titleBar_);
        Q_ASSERT(titleBarAdded);
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
        // A close-only utility window must position the single traffic light
        // explicitly. Centering it in the three-button reservation can move
        // it outside the visible title surface on compact dialogs.
        windowAgent_->setSystemButtonPosition(VkWindowAgent::SystemButton::Close,
                                              kMacCloseButtonPosition);
#endif
        windowAgent_->setHitTestVisible(fallbackCloseButton_, true);
    }

    refreshCloseButtonPlacement();
}

void VkFramelessDialog::refreshCloseButtonPlacement() {
    const bool usePlatformClose =
        closeButtonPlacement_ == CloseButtonPlacement::Platform && platformCloseAvailable_;
    const bool useFallbackClose =
        closeButtonPlacement_ == CloseButtonPlacement::Trailing ||
        (closeButtonPlacement_ == CloseButtonPlacement::Platform && !platformCloseAvailable_);
    if (windowAgent_ != nullptr) {
        windowAgent_->setSystemButtonVisibility(
            usePlatformClose ? VkWindowAgent::SystemButtonVisibility::AlwaysVisible
                             : VkWindowAgent::SystemButtonVisibility::AlwaysHidden);
    }
    if (fallbackCloseButton_ != nullptr) {
        fallbackCloseButton_->setVisible(useFallbackClose);
    }
    if (nativeButtonReserve_ != nullptr) {
        nativeButtonReserve_->setVisible(usePlatformClose);
    }
}

void VkFramelessDialog::refreshTitle() {
    if (titleLabel_ != nullptr) {
        titleLabel_->setText(windowTitle());
    }
}

} // namespace vkui
