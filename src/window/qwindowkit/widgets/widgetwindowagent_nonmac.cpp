// Copyright (C) 2023-2024 Stdware Collections (https://www.github.com/stdware)
// Copyright (C) 2021-2023 wangwenx190 (Yuhang Zhao)
// SPDX-License-Identifier: Apache-2.0

#include "widgetwindowagent_p.h"

#include <QtCore/QEvent>
#include <QtGui/QCursor>
#include <QtWidgets/QApplication>

namespace QWK {

    class SystemButtonVisibilityEventFilter final : public QObject {
    public:
        explicit SystemButtonVisibilityEventFilter(WidgetWindowAgentPrivate *d)
            : d(d) {
            qApp->installEventFilter(this);
        }

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override {
            const auto type = event->type();
            if (type != QEvent::Enter && type != QEvent::Leave &&
                type != QEvent::MouseMove && type != QEvent::HoverMove &&
                type != QEvent::Move && type != QEvent::Resize &&
                type != QEvent::WindowActivate && type != QEvent::WindowDeactivate &&
                type != QEvent::Show) {
                return false;
            }

            const auto *widget = qobject_cast<const QWidget *>(watched);
            if (widget && widget->window() == d->hostWidget) {
                d->updateSystemButtonVisibility();
            }
            return false;
        }

    private:
        WidgetWindowAgentPrivate *d;
    };

    void WidgetWindowAgentPrivate::setupSystemButtonVisibility() {
        systemButtonVisibilityEventFilter =
            std::make_unique<SystemButtonVisibilityEventFilter>(this);
    }

    void WidgetWindowAgentPrivate::updateSystemButtonVisibility() {
        if (!hostWidget || !context) {
            return;
        }

        const auto visibility = context->systemButtonVisibility();
        bool showButtons = visibility == WindowAgentBase::AlwaysVisible;

        if (visibility == WindowAgentBase::VisibleOnHover) {
            QRect hoverRect;
            for (int i = WindowAgentBase::WindowIcon; i <= WindowAgentBase::Close; ++i) {
                auto *button = qobject_cast<QWidget *>(
                    context->systemButton(static_cast<WindowAgentBase::SystemButton>(i)));
                if (!button || button->window() != hostWidget || !button->isEnabled()) {
                    continue;
                }

                const QRect buttonRect(button->mapToGlobal(QPoint()), button->size());
                hoverRect = hoverRect.isNull() ? buttonRect : hoverRect.united(buttonRect);
            }
            showButtons = !hoverRect.isNull() &&
                          hoverRect.adjusted(-4, -4, 4, 4).contains(QCursor::pos());
        }

        for (int i = WindowAgentBase::WindowIcon; i <= WindowAgentBase::Close; ++i) {
            auto *button = qobject_cast<QWidget *>(
                context->systemButton(static_cast<WindowAgentBase::SystemButton>(i)));
            if (button) {
                button->setVisible(showButtons && button->isEnabled());
            }
        }
    }

}
