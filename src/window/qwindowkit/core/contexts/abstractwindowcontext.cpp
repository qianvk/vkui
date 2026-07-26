// Copyright (C) 2021-2023 wangwenx190 (Yuhang Zhao)
// Copyright (C) 2023-2024 Stdware Collections (https://www.github.com/stdware)
// SPDX-License-Identifier: Apache-2.0
// Modified by the VkUI project for direct source integration.

#include "abstractwindowcontext_p.h"

#include <QtGui/QPen>
#include <QtGui/QPainter>
#include <QtGui/QScreen>

#include "qwkglobal_p.h"

namespace QWK {

    AbstractWindowContext::AbstractWindowContext() = default;

    AbstractWindowContext::~AbstractWindowContext() = default;

    void AbstractWindowContext::setup(QObject *host, WindowItemDelegate *delegate) {
        if (m_host || !host || !delegate) {
            return;
        }
        m_host = host;
        m_delegate.reset(delegate);
        m_winIdChangeEventFilter.reset(delegate->createWinIdEventFilter(host, this));
        notifyWinIdChange();
    }

    bool AbstractWindowContext::isHitTestVisible(QObject *titleBar, const QObject *obj) const
    {
        if (!titleBar || !obj) {
            return false;
        }

        const auto* record = findTitleBarRecord(titleBar);
        if (!record) {
            return false;
        }

        return record->hitTestVisibleItems.contains(obj);
    }

    bool AbstractWindowContext::setHitTestVisible(QObject *titleBar, QObject *obj, bool visible)
    {
        if (!titleBar || !obj) {
            return false;
        }

        auto *record = findTitleBarRecord(titleBar);
        if (!record) {
            return false;
        }

        // Sibling controls such as QSplitterHandle may overlap a title bar. They are valid
        // exclusions as long as both objects belong to this top-level window.
        if (!m_delegate->isInHostWindow(titleBar, m_host) ||
            !m_delegate->isInHostWindow(obj, m_host)) {
            return false;
        }

        if (!visible) {
            record->hitTestVisibleItems.removeAll(obj);
        } else if (!record->hitTestVisibleItems.contains(obj)) {
            record->hitTestVisibleItems.append(obj);
        }

        return true;
    }

    bool AbstractWindowContext::isHitTestVisible(const QObject *obj) const {
        if (!obj) {
            return false;
        }
        for (const auto &record : m_titleBars) {
            if (record.hitTestVisibleItems.contains(obj)) {
                return true;
            }
        }
        return false;
    }

    bool AbstractWindowContext::setHitTestVisible(QObject *obj, bool visible) {
        auto *bar = titleBar();
        return bar && setHitTestVisible(bar, obj, visible);
    }

    bool AbstractWindowContext::setSystemButton(WindowAgentBase::SystemButton button,
                                                QObject *obj) {
        Q_ASSERT(button > WindowAgentBase::Unknown && button <= WindowAgentBase::Close);
        if (button <= WindowAgentBase::Unknown || button > WindowAgentBase::Close) {
            return false;
        }

        auto org = m_systemButtons[button];
        if (org == obj) {
            return false;
        }
        m_systemButtons[button] = obj;
        return true;
    }

    bool AbstractWindowContext::setSystemButtonVisibility(
        WindowAgentBase::SystemButtonVisibility visibility) {
        if (visibility < WindowAgentBase::AlwaysVisible ||
            visibility > WindowAgentBase::AlwaysHidden ||
            m_systemButtonVisibility == visibility) {
            return false;
        }

        m_systemButtonVisibility = visibility;
        if (m_windowId) {
            virtual_hook(SystemButtonVisibilityChangedHook, nullptr);
        }
        return true;
    }

    bool AbstractWindowContext::setResizable(bool resizable) {
        if (m_resizable == resizable) {
            return false;
        }
        m_resizable = resizable;
        if (m_windowId) {
            virtual_hook(ResizableChangedHook, nullptr);
        }
        return true;
    }

#ifdef Q_OS_MAC
    void AbstractWindowContext::setSystemButtonAreaCallback(const ScreenRectCallback &callback) {
        m_systemButtonAreaCallback = callback;
        virtual_hook(SystemButtonAreaChangedHook, nullptr);
    }

    bool AbstractWindowContext::setSystemButtonPosition(WindowAgentBase::SystemButton button,
                                                        const QPoint &position, bool enabled) {
        if (button != WindowAgentBase::Close && button != WindowAgentBase::Minimize &&
            button != WindowAgentBase::Maximize) {
            return false;
        }
        if (m_hasSystemButtonPositions[button] == enabled &&
            (!enabled || m_systemButtonPositions[button] == position)) {
            return false;
        }
        m_hasSystemButtonPositions[button] = enabled;
        m_systemButtonPositions[button] = enabled ? position : QPoint();
        virtual_hook(SystemButtonPositionChangedHook, nullptr);
        return true;
    }
#endif

    bool AbstractWindowContext::isInSystemButtons(const QPoint &pos,
                                                  WindowAgentBase::SystemButton *button) const {
        *button = WindowAgentBase::Unknown;
        for (int i = WindowAgentBase::WindowIcon; i <= WindowAgentBase::Close; ++i) {
            auto currentButton = m_systemButtons[static_cast<std::size_t>(i)];
            if (!currentButton || !m_delegate->isEnabled(currentButton)) {
                continue;
            }

            // Windows must retain native hit testing while hover-only caption buttons are hidden.
            // Its existing non-client event bridge then delivers mouse movement back to Qt, which
            // lets WidgetWindowAgent reveal the registered buttons.
            if (!m_delegate->isVisible(currentButton) &&
                m_systemButtonVisibility != WindowAgentBase::VisibleOnHover) {
                continue;
            }
            if (m_delegate->mapGeometryToScene(currentButton).contains(pos)) {
                *button = static_cast<WindowAgentBase::SystemButton>(i);
                return true;
            }
        }
        return false;
    }

    bool AbstractWindowContext::isInTitleBarDraggableArea(const QPoint &pos) const {
        if (!m_windowHandle) {
            return false;
        }

        if (m_titleBars.empty()) {
            // There's no title bar at all, the mouse will always be in the client area.
            return false;
        }

        WindowAgentBase::SystemButton button;
        if (isInSystemButtons(pos, &button)) {
            return false;
        }

        QRect windowRect = {QPoint(0, 0), m_windowHandle->size()};
        for (const auto& record : m_titleBars) {
            if (!record.titleBar || !m_delegate->isVisible(record.titleBar) ||
                !m_delegate->isEnabled(record.titleBar)) {
                // The title bar is hidden or disabled for some reason, treat it as there's
                // no title bar.
                continue;
            }

            QRect titleBarRect = m_delegate->mapGeometryToScene(record.titleBar);
            if (!titleBarRect.intersects(windowRect)) {
                // The title bar is totally outside the window for some reason,
                // also treat it as there's no title bar.
                continue;
            }

            if (!titleBarRect.contains(pos)) {
                continue;
            }

            for (auto &&item : std::as_const(record.hitTestVisibleItems)) {
                if (item && m_delegate->isVisible(item) && m_delegate->isEnabled(item) &&
                    m_delegate->mapGeometryToScene(item).contains(pos)) {
                    return false;
                }
            }

            return true;
        }

        return false;
    }

    QString AbstractWindowContext::key() const {
        return {};
    }

    QWK_USED static constexpr const struct {
        const quint32 activeLight = MAKE_RGBA_COLOR(210, 233, 189, 226);
        const quint32 activeDark = MAKE_RGBA_COLOR(177, 205, 190, 240);
        const quint32 inactiveLight = MAKE_RGBA_COLOR(193, 195, 211, 203);
        const quint32 inactiveDark = MAKE_RGBA_COLOR(240, 240, 250, 255);
    } kSampleColorSet;

    void AbstractWindowContext::virtual_hook(int id, void *data) {
        switch (id) {
            case CentralizeHook: {
                if (!m_windowId)
                    return;

                QRect windowGeometry = m_delegate->getGeometry(m_host);
                QRect screenGeometry = m_windowHandle->screen()->geometry();
                int x = (screenGeometry.width() - windowGeometry.width()) / 2;
                int y = (screenGeometry.height() - windowGeometry.height()) / 2;
                QPoint pos(x, y);
                pos += screenGeometry.topLeft();
                m_delegate->setGeometry(m_host, QRect(pos, windowGeometry.size()));
                return;
            }

            case RaiseWindowHook: {
                if (!m_windowId)
                    return;

                m_delegate->setWindowVisible(m_host, true);
                Qt::WindowStates state = m_delegate->getWindowState(m_host);
                if (state & Qt::WindowMinimized) {
                    m_delegate->setWindowState(m_host, state & ~Qt::WindowMinimized);
                }
                m_delegate->bringWindowToTop(m_host);
                return;
            }

            case DefaultColorsHook: {
                auto &map = *static_cast<QMap<QString, QColor> *>(data);
                map.clear();
                map.insert(QStringLiteral("activeLight"), kSampleColorSet.activeLight);
                map.insert(QStringLiteral("activeDark"), kSampleColorSet.activeDark);
                map.insert(QStringLiteral("inactiveLight"), kSampleColorSet.inactiveLight);
                map.insert(QStringLiteral("inactiveDark"), kSampleColorSet.inactiveDark);
                return;
            }

            default:
                break;
        }
    }

    void AbstractWindowContext::showSystemMenu(const QPoint &pos) {
        virtual_hook(ShowSystemMenuHook, &const_cast<QPoint &>(pos));
    }

    void AbstractWindowContext::notifyWinIdChange() {
        auto oldWinId = m_windowId;
        m_windowId = m_winIdChangeEventFilter->winId();

        // In Qt6, after QWidget::close() is called, the related QWindow's all surfaces and the
        // platform window will be removed, and the WinId will be set to 0. After that, when the
        // QWidget is shown again, the whole things will be recreated again.
        // As a result, we must update our WindowContext each time the WinId changes.
        if (m_windowHandle) {
            removeEventFilter(m_windowHandle);
        }
        m_windowHandle = m_delegate->hostWindow(m_host);
        if (m_windowHandle) {
            m_windowHandle->installEventFilter(this);
        }

        if (oldWinId != m_windowId) {
            winIdChanged(m_windowId, oldWinId);

            if (m_windowId) {
                // Refresh window attributes
                for (auto it = m_windowAttributesOrder.begin();
                     it != m_windowAttributesOrder.end();) {
                    if (!windowAttributeChanged(it->first, it->second, {})) {
                        m_windowAttributes.remove(it->first);
                        it = m_windowAttributesOrder.erase(it);
                        continue;
                    }
                    ++it;
                }
            }

            // Send to shared dispatchers
            QEvent e(QEvent::WinIdChange);
            sharedDispatch(m_host, &e);
        }
    }

    QVariant AbstractWindowContext::windowAttribute(const QString &key) const {
        auto it = m_windowAttributes.find(key);
        if (it == m_windowAttributes.end()) {
            return {};
        }
        return it.value()->second;
    }

    bool AbstractWindowContext::setWindowAttribute(const QString &key, const QVariant &attribute) {
        auto it = m_windowAttributes.find(key);
        if (it == m_windowAttributes.end()) {
            if (!attribute.isValid()) {
                return true;
            }
            if (m_windowId && !windowAttributeChanged(key, attribute, {})) {
                return false;
            }
            m_windowAttributes.insert(
                key, m_windowAttributesOrder.insert(m_windowAttributesOrder.end(),
                                                    std::make_pair(key, attribute)));
            return true;
        }

        auto &listIter = it.value();
        auto &oldAttr = listIter->second;
        if (m_windowId && !windowAttributeChanged(key, attribute, oldAttr)) {
            return false;
        }

        if (attribute.isValid()) {
            oldAttr = attribute;
            m_windowAttributesOrder.splice(m_windowAttributesOrder.end(), m_windowAttributesOrder,
                                           listIter);
        } else {
            m_windowAttributesOrder.erase(listIter);
            m_windowAttributes.erase(it);
        }
        return true;
    }

    bool AbstractWindowContext::eventFilter(QObject *obj, QEvent *event) {
        if (obj == m_windowHandle && sharedDispatch(obj, event)) {
            return true;
        }
        return QObject::eventFilter(obj, event);
    }

    bool AbstractWindowContext::windowAttributeChanged(const QString &key,
                                                       const QVariant &attribute,
                                                       const QVariant &oldAttribute) {
        Q_UNUSED(key)
        Q_UNUSED(attribute)
        Q_UNUSED(oldAttribute)
        return false;
    }

    const AbstractWindowContext::TitleBarRecord *AbstractWindowContext::findTitleBarRecord(const QObject *titleBar) const
    {
        for (const auto& item : m_titleBars) {
            if (item.titleBar == titleBar) {
                return &item;
            }
        }

        return nullptr;
    }

    AbstractWindowContext::TitleBarRecord *AbstractWindowContext::findTitleBarRecord(const QObject *titleBar)
    {
        for (auto& item : m_titleBars) {
            if (item.titleBar == titleBar) {
                return &item;
            }
        }

        return nullptr;
    }

}

bool QWK::AbstractWindowContext::addTitleBar(QObject *titleBar)
{
    Q_ASSERT(titleBar);
    if (!titleBar || findTitleBarRecord(titleBar)) {
        return false;
    }

    m_titleBars.append(TitleBarRecord{.titleBar = titleBar});
    return true;
}

QObject *QWK::AbstractWindowContext::titleBar() const
{
    for (const auto &record : m_titleBars) {
        if (record.titleBar) {
            return record.titleBar;
        }
    }
    return nullptr;
}

bool QWK::AbstractWindowContext::setTitleBar(QObject *titleBar)
{
    Q_ASSERT(titleBar);
    if (!titleBar || (m_titleBars.size() == 1 && m_titleBars.constFirst().titleBar == titleBar)) {
        return false;
    }

    clearTitleBars();
    for (auto &button : m_systemButtons) {
        button = nullptr;
    }
    return addTitleBar(titleBar);
}



QList<QObject *> QWK::AbstractWindowContext::titleBars() const
{
    QList<QObject *> titleBars;
    titleBars.reserve(m_titleBars.size());

    for (const auto& record : m_titleBars) {
        if (record.titleBar) {
            titleBars.append(record.titleBar);
        }
    }

    return titleBars;
}



bool QWK::AbstractWindowContext::removeTitleBar(QObject *titleBar)
{
    if (!titleBar) {
        return false;
    }

    return erase_if(m_titleBars, [titleBar](const auto& record) {
               return record.titleBar == titleBar;
           }) > 0;
}



void QWK::AbstractWindowContext::clearTitleBars()
{
    m_titleBars.clear();
}

bool QWK::AbstractWindowContext::isTitleBarRegistered(const QObject *titleBar) const
{
    return findTitleBarRecord(titleBar);
}
