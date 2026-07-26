// Copyright (C) 2023-2024 Stdware Collections (https://www.github.com/stdware)
// Copyright (C) 2021-2023 wangwenx190 (Yuhang Zhao)
// SPDX-License-Identifier: Apache-2.0
// Modified by the VkUI project for direct source integration.

#ifndef WINDOWAGENTBASE_H
#define WINDOWAGENTBASE_H

#include <memory>

#include <QtCore/QObject>

#include "qwkglobal.h"

namespace QWK {

    class WindowAgentBasePrivate;

    class QWK_CORE_EXPORT WindowAgentBase : public QObject {
        Q_OBJECT
        Q_PROPERTY(bool resizable READ isResizable WRITE setResizable NOTIFY resizableChanged)
        Q_PROPERTY(SystemButtonVisibility systemButtonVisibility READ systemButtonVisibility WRITE
                       setSystemButtonVisibility NOTIFY systemButtonVisibilityChanged)
        Q_DECLARE_PRIVATE(WindowAgentBase)
    public:
        ~WindowAgentBase() override;

        enum SystemButton {
            Unknown,
            WindowIcon,
            Help,
            Minimize,
            Maximize,
            Close,
        };
        Q_ENUM(SystemButton)

        enum SystemButtonVisibility {
            AlwaysVisible, ///< Keep the system buttons visible.
            VisibleOnHover, ///< Show the system buttons while their area is hovered.
            AlwaysHidden, ///< Keep the system buttons hidden.
        };
        Q_ENUM(SystemButtonVisibility)

        SystemButtonVisibility systemButtonVisibility() const;
        void setSystemButtonVisibility(SystemButtonVisibility visibility);

        bool isResizable() const;
        void setResizable(bool resizable);

        QVariant windowAttribute(const QString &key) const;
        Q_INVOKABLE bool setWindowAttribute(const QString &key, const QVariant &attribute);

    Q_SIGNALS:
        void resizableChanged(bool resizable);
        void systemButtonVisibilityChanged(SystemButtonVisibility visibility);

    public Q_SLOTS:
        void showSystemMenu(const QPoint &pos); // Not available on macOS.
        void centralize();
        void raise();

    protected:
        explicit WindowAgentBase(WindowAgentBasePrivate &d, QObject *parent = nullptr);

        const std::unique_ptr<WindowAgentBasePrivate> d_ptr;
    };

}

#endif // WINDOWAGENTBASE_H
