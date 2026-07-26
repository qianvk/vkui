// SPDX-License-Identifier: MIT

#include "widgetwindowagent.h"

#include <QPointer>
#include <QSignalBlocker>
#include <QThread>
#include <QWidget>
#include <vkui/window/VkWindowAgent.h>

namespace vkui {
namespace {

QWK::WindowAgentBase::SystemButton toNative(VkWindowAgent::SystemButton button) {
    switch (button) {
    case VkWindowAgent::SystemButton::WindowIcon:
        return QWK::WindowAgentBase::WindowIcon;
    case VkWindowAgent::SystemButton::Help:
        return QWK::WindowAgentBase::Help;
    case VkWindowAgent::SystemButton::Minimize:
        return QWK::WindowAgentBase::Minimize;
    case VkWindowAgent::SystemButton::Maximize:
        return QWK::WindowAgentBase::Maximize;
    case VkWindowAgent::SystemButton::Close:
        return QWK::WindowAgentBase::Close;
    case VkWindowAgent::SystemButton::Unknown:
    default:
        return QWK::WindowAgentBase::Unknown;
    }
}

VkWindowAgent::SystemButton fromNative(QWK::WindowAgentBase::SystemButton button) {
    switch (button) {
    case QWK::WindowAgentBase::WindowIcon:
        return VkWindowAgent::SystemButton::WindowIcon;
    case QWK::WindowAgentBase::Help:
        return VkWindowAgent::SystemButton::Help;
    case QWK::WindowAgentBase::Minimize:
        return VkWindowAgent::SystemButton::Minimize;
    case QWK::WindowAgentBase::Maximize:
        return VkWindowAgent::SystemButton::Maximize;
    case QWK::WindowAgentBase::Close:
        return VkWindowAgent::SystemButton::Close;
    case QWK::WindowAgentBase::Unknown:
    default:
        return VkWindowAgent::SystemButton::Unknown;
    }
}

QWK::WindowAgentBase::SystemButtonVisibility
toNative(VkWindowAgent::SystemButtonVisibility visibility) {
    switch (visibility) {
    case VkWindowAgent::SystemButtonVisibility::VisibleOnHover:
        return QWK::WindowAgentBase::VisibleOnHover;
    case VkWindowAgent::SystemButtonVisibility::AlwaysHidden:
        return QWK::WindowAgentBase::AlwaysHidden;
    case VkWindowAgent::SystemButtonVisibility::AlwaysVisible:
    default:
        return QWK::WindowAgentBase::AlwaysVisible;
    }
}

VkWindowAgent::SystemButtonVisibility
fromNative(QWK::WindowAgentBase::SystemButtonVisibility visibility) {
    switch (visibility) {
    case QWK::WindowAgentBase::VisibleOnHover:
        return VkWindowAgent::SystemButtonVisibility::VisibleOnHover;
    case QWK::WindowAgentBase::AlwaysHidden:
        return VkWindowAgent::SystemButtonVisibility::AlwaysHidden;
    case QWK::WindowAgentBase::AlwaysVisible:
    default:
        return VkWindowAgent::SystemButtonVisibility::AlwaysVisible;
    }
}

bool isValidSystemButton(const VkWindowAgent::SystemButton button) noexcept {
    return button >= VkWindowAgent::SystemButton::WindowIcon &&
           button <= VkWindowAgent::SystemButton::Close;
}

bool isValidVisibility(const VkWindowAgent::SystemButtonVisibility visibility) noexcept {
    return visibility >= VkWindowAgent::SystemButtonVisibility::AlwaysVisible &&
           visibility <= VkWindowAgent::SystemButtonVisibility::AlwaysHidden;
}

} // namespace

class VkWindowAgentPrivate final {
  public:
    explicit VkWindowAgentPrivate(VkWindowAgent* owner)
        : native(new QWK::WidgetWindowAgent(owner)) {}

    [[nodiscard]] bool isReady() const noexcept {
        return wasSetup && !host.isNull();
    }

    [[nodiscard]] bool belongsToHost(const QWidget* widget) const noexcept {
        return isReady() && widget != nullptr && widget->window() == host;
    }

    QWK::WidgetWindowAgent* native = nullptr;
    QPointer<QWidget> host;
    VkWindowAgent::SystemButtonVisibility visibility =
        VkWindowAgent::SystemButtonVisibility::AlwaysVisible;
    bool resizable = false;
    bool wasSetup = false;
};

VkWindowAgent::VkWindowAgent(QObject* parent)
    : QObject(parent), d_(std::make_unique<VkWindowAgentPrivate>(this)) {
    connect(d_->native, &QWK::WindowAgentBase::resizableChanged, this,
            [this](const bool resizable) {
                if (d_->resizable == resizable) {
                    return;
                }
                d_->resizable = resizable;
                emit resizableChanged(resizable);
            });
    connect(d_->native, &QWK::WindowAgentBase::systemButtonVisibilityChanged, this,
            [this](QWK::WindowAgentBase::SystemButtonVisibility visibility) {
                const SystemButtonVisibility converted = fromNative(visibility);
                if (d_->visibility == converted) {
                    return;
                }
                d_->visibility = converted;
                emit systemButtonVisibilityChanged(converted);
            });
    connect(d_->native, &QWK::WidgetWindowAgent::titleBarChanged, this,
            &VkWindowAgent::titleBarChanged);
    connect(d_->native, &QWK::WidgetWindowAgent::titleBarAdded, this,
            &VkWindowAgent::titleBarAdded);
    connect(d_->native, &QWK::WidgetWindowAgent::titleBarRemoved, this,
            &VkWindowAgent::titleBarRemoved);
    connect(d_->native, &QWK::WidgetWindowAgent::titleBarsCleared, this,
            &VkWindowAgent::titleBarsCleared);
    connect(d_->native, &QWK::WidgetWindowAgent::systemButtonChanged, this,
            [this](QWK::WindowAgentBase::SystemButton button, QWidget* widget) {
                emit systemButtonChanged(fromNative(button), widget);
            });
}

VkWindowAgent::~VkWindowAgent() = default;

bool VkWindowAgent::setup(QWidget* window) {
    if (window == nullptr || d_->wasSetup || !window->isWindow() ||
        QThread::currentThread() != window->thread() || thread() != window->thread()) {
        return false;
    }
    if (!d_->native->setup(window)) {
        return false;
    }

    d_->host = window;
    d_->wasSetup = true;
    const QSignalBlocker blockNativeSignals(d_->native);
    d_->native->setSystemButtonVisibility(toNative(d_->visibility));
    return true;
}

QList<QWidget*> VkWindowAgent::titleBars() const {
    return d_->isReady() ? d_->native->titleBars() : QList<QWidget*>{};
}

QWidget* VkWindowAgent::titleBar() const {
    return d_->isReady() ? d_->native->titleBar() : nullptr;
}

void VkWindowAgent::setTitleBar(QWidget* titleBar) {
    if (!d_->isReady()) {
        return;
    }
    if (titleBar == nullptr) {
        d_->native->clearTitleBars();
        return;
    }
    if (d_->belongsToHost(titleBar)) {
        d_->native->setTitleBar(titleBar);
    }
}

bool VkWindowAgent::addTitleBar(QWidget* titleBar) {
    return d_->belongsToHost(titleBar) && d_->native->addTitleBar(titleBar);
}

bool VkWindowAgent::removeTitleBar(QWidget* titleBar) {
    return d_->isReady() && titleBar != nullptr && d_->native->removeTitleBar(titleBar);
}

void VkWindowAgent::clearTitleBars() {
    if (d_->isReady()) {
        d_->native->clearTitleBars();
    }
}

QWidget* VkWindowAgent::systemButton(SystemButton button) const {
    return d_->isReady() && isValidSystemButton(button) ? d_->native->systemButton(toNative(button))
                                                        : nullptr;
}

void VkWindowAgent::setSystemButton(SystemButton button, QWidget* widget) {
    if (d_->isReady() && isValidSystemButton(button) &&
        (widget == nullptr || d_->belongsToHost(widget))) {
        d_->native->setSystemButton(toNative(button), widget);
    }
}

bool VkWindowAgent::installSystemButtons() {
    return d_->isReady() && d_->native->installSystemButtons();
}

QRect VkWindowAgent::systemButtonAreaGeometry() const {
    return d_->isReady() ? d_->native->systemButtonAreaGeometry() : QRect{};
}

QWidget* VkWindowAgent::systemButtonArea() const {
#ifdef Q_OS_MAC
    return d_->isReady() ? d_->native->systemButtonArea() : nullptr;
#else
    return nullptr;
#endif
}

void VkWindowAgent::setSystemButtonArea(QWidget* widget) {
#ifdef Q_OS_MAC
    if (d_->isReady() && (widget == nullptr || d_->belongsToHost(widget))) {
        d_->native->setSystemButtonArea(widget);
    }
#else
    Q_UNUSED(widget)
#endif
}

void VkWindowAgent::setSystemButtonAreaGeometry(const QRect& rect) {
#ifdef Q_OS_MAC
    if (d_->isReady()) {
        d_->native->setSystemButtonAreaGeometry(rect);
    }
#else
    Q_UNUSED(rect)
#endif
}

VkWindowAgent::ScreenRectCallback VkWindowAgent::systemButtonAreaCallback() const {
#ifdef Q_OS_MAC
    return d_->isReady() ? d_->native->systemButtonAreaCallback() : ScreenRectCallback{};
#else
    return {};
#endif
}

void VkWindowAgent::setSystemButtonAreaCallback(ScreenRectCallback callback) {
#ifdef Q_OS_MAC
    if (d_->isReady()) {
        d_->native->setSystemButtonAreaCallback(std::move(callback));
    }
#else
    Q_UNUSED(callback)
#endif
}

bool VkWindowAgent::hasSystemButtonPosition(SystemButton button) const {
#ifdef Q_OS_MAC
    return d_->isReady() && isValidSystemButton(button) &&
           d_->native->hasSystemButtonPosition(toNative(button));
#else
    Q_UNUSED(button)
    return false;
#endif
}

QPoint VkWindowAgent::systemButtonPosition(SystemButton button) const {
#ifdef Q_OS_MAC
    return d_->isReady() && isValidSystemButton(button)
               ? d_->native->systemButtonPosition(toNative(button))
               : QPoint{};
#else
    Q_UNUSED(button)
    return {};
#endif
}

void VkWindowAgent::setSystemButtonPosition(SystemButton button, const QPoint& position) {
#ifdef Q_OS_MAC
    if (d_->isReady() && isValidSystemButton(button)) {
        d_->native->setSystemButtonPosition(toNative(button), position);
    }
#else
    Q_UNUSED(button)
    Q_UNUSED(position)
#endif
}

void VkWindowAgent::clearSystemButtonPosition(SystemButton button) {
#ifdef Q_OS_MAC
    if (d_->isReady() && isValidSystemButton(button)) {
        d_->native->clearSystemButtonPosition(toNative(button));
    }
#else
    Q_UNUSED(button)
#endif
}

bool VkWindowAgent::isHitTestVisible(QWidget* titleBar, const QWidget* widget) const {
    return d_->isReady() && d_->native->isHitTestVisible(titleBar, widget);
}

bool VkWindowAgent::setHitTestVisible(QWidget* titleBar, QWidget* widget, bool visible) {
    return d_->isReady() && d_->native->setHitTestVisible(titleBar, widget, visible);
}

bool VkWindowAgent::isHitTestVisible(const QWidget* widget) const {
    return d_->isReady() && d_->native->isHitTestVisible(widget);
}

void VkWindowAgent::setHitTestVisible(QWidget* widget, bool visible) {
    if (d_->isReady()) {
        d_->native->setHitTestVisible(widget, visible);
    }
}

bool VkWindowAgent::isResizable() const {
    return d_->resizable;
}

void VkWindowAgent::setResizable(bool resizable) {
    if (d_->wasSetup && !d_->isReady()) {
        if (d_->resizable != resizable) {
            d_->resizable = resizable;
            emit resizableChanged(resizable);
        }
        return;
    }
    d_->native->setResizable(resizable);
}

VkWindowAgent::SystemButtonVisibility VkWindowAgent::systemButtonVisibility() const {
    return d_->visibility;
}

void VkWindowAgent::setSystemButtonVisibility(SystemButtonVisibility visibility) {
    if (!isValidVisibility(visibility) || d_->visibility == visibility) {
        return;
    }
    if (!d_->isReady()) {
        d_->visibility = visibility;
        emit systemButtonVisibilityChanged(visibility);
        return;
    }
    d_->native->setSystemButtonVisibility(toNative(visibility));
}

QVariant VkWindowAgent::windowAttribute(const QString& key) const {
    return d_->isReady() ? d_->native->windowAttribute(key) : QVariant{};
}

bool VkWindowAgent::setWindowAttribute(const QString& key, const QVariant& value) {
    return d_->isReady() && d_->native->setWindowAttribute(key, value);
}

void VkWindowAgent::showSystemMenu(const QPoint& position) {
    if (d_->isReady()) {
        d_->native->showSystemMenu(position);
    }
}

void VkWindowAgent::centralize() {
    if (d_->isReady()) {
        d_->native->centralize();
    }
}

void VkWindowAgent::raiseWindow() {
    if (d_->isReady()) {
        d_->native->raise();
    }
}

} // namespace vkui
