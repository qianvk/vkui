// Copyright (C) 2023-2024 Stdware Collections (https://www.github.com/stdware)
// Copyright (C) 2021-2023 wangwenx190 (Yuhang Zhao)
// SPDX-License-Identifier: Apache-2.0
// Modified by the VkUI project for direct source integration.

#ifndef WIDGETWINDOWAGENT_H
#define WIDGETWINDOWAGENT_H

#include <QtWidgets/QWidget>

#include "windowagentbase.h"
#include "qwkwidgetsglobal.h"

namespace QWK {

    class WidgetWindowAgentPrivate;

    class QWK_WIDGETS_EXPORT WidgetWindowAgent : public WindowAgentBase {
        Q_OBJECT
        Q_DECLARE_PRIVATE(WidgetWindowAgent)
    public:
        explicit WidgetWindowAgent(QObject *parent = nullptr);
        ~WidgetWindowAgent() override;

    public:
        bool setup(QWidget *w);

        QList<QWidget *> titleBars() const;

        QWidget *titleBar() const;
        void setTitleBar(QWidget *titleBar);

        bool addTitleBar(QWidget *titleBar);
        bool removeTitleBar(QWidget *titleBar);
        void clearTitleBars();

        QWidget *systemButton(SystemButton button) const;
        void setSystemButton(SystemButton button, QWidget *w);

        bool installSystemButtons();
        QRect systemButtonAreaGeometry() const;

#ifdef Q_OS_MAC
        // The native traffic-light buttons are centered in this widget's window-space geometry.
        QWidget *systemButtonArea() const;
        void setSystemButtonArea(QWidget *widget);

        void setSystemButtonAreaGeometry(const QRect &rect);

        ScreenRectCallback systemButtonAreaCallback() const;
        void setSystemButtonAreaCallback(const ScreenRectCallback &callback);

        // Positions one AppKit traffic-light button by its top-left point in host coordinates.
        bool hasSystemButtonPosition(SystemButton button) const;
        QPoint systemButtonPosition(SystemButton button) const;
        void setSystemButtonPosition(SystemButton button, const QPoint &position);
        void clearSystemButtonPosition(SystemButton button);
#endif

        bool isHitTestVisible(QWidget *titleBar, const QWidget *w) const;
        bool setHitTestVisible(QWidget *titleBar, QWidget *w, bool visible = true);
        bool isHitTestVisible(const QWidget *w) const;
        void setHitTestVisible(QWidget *w, bool visible = true);

    Q_SIGNALS:
        void titleBarChanged(QWidget *w);
        void titleBarAdded(QWidget *w);
        void titleBarRemoved(QWidget *w);
        void titleBarsCleared();
        void systemButtonChanged(SystemButton button, QWidget *w);

    protected:
        WidgetWindowAgent(WidgetWindowAgentPrivate &d, QObject *parent = nullptr);
    };

}

#endif // WIDGETWINDOWAGENT_H
