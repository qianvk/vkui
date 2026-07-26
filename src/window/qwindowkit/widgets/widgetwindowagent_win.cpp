// Copyright (C) 2023-2024 Stdware Collections (https://www.github.com/stdware)
// Copyright (C) 2021-2023 wangwenx190 (Yuhang Zhao)
// SPDX-License-Identifier: Apache-2.0
// Modified by the VkUI project for direct source integration.

#include "widgetwindowagent_p.h"

#include <algorithm>

#include <QtCore/QCoreApplication>
#include <QtCore/QEvent>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QIcon>
#include <QtGui/QPainter>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QPushButton>

#include "qwindowkit_windows.h"

#if QWINDOWKIT_CONFIG(ENABLE_WINDOWS_SYSTEM_BORDERS)
#  include <QtCore/private/qcoreapplication_p.h>
#  include "qwkglobal_p.h"
#  include "windows10borderhandler_p.h"
#endif

static void initializeWindowsCaptionButtonResources() {
    Q_INIT_RESOURCE(windows_caption_buttons);
}

namespace QWK {

    namespace {

        constexpr int kCaptionButtonWidth = 46;
        constexpr int kCaptionButtonHeight = 32;

        enum class CaptionButtonRole {
            Minimize,
            Maximize,
            Close,
        };

        HWND windowHandleForWidget(QWidget *widget) {
            if (!widget) {
                return nullptr;
            }
            QWidget *window = widget->window();
            return window ? reinterpret_cast<HWND>(window->internalWinId()) : nullptr;
        }

        void postSystemCommand(QWidget *host, WPARAM command) {
            if (const auto hwnd = windowHandleForWidget(host)) {
                // Use the same native command path as non-client caption buttons.
                ::PostMessageW(hwnd, WM_SYSCOMMAND, command, 0);
            }
        }

        class WindowsCaptionButton final : public QPushButton {
        public:
            explicit WindowsCaptionButton(CaptionButtonRole role, QWidget *parent = nullptr)
                : QPushButton(parent), m_role(role) {
                setFocusPolicy(Qt::NoFocus);
                setFlat(true);
                setCursor(Qt::ArrowCursor);
                setFixedSize(kCaptionButtonWidth, kCaptionButtonHeight);
                setIconSize(role == CaptionButtonRole::Maximize ? QSize(12, 12) : QSize(11, 11));

                switch (role) {
                    case CaptionButtonRole::Minimize:
                        m_icon = QIcon(
                            QStringLiteral(":/qwindowkit/widgets/windows/caption/minimize.svg"));
                        setAccessibleName(
                            QCoreApplication::translate("QWK::WindowsCaptionButton", "Minimize"));
                        break;
                    case CaptionButtonRole::Maximize:
                        m_icon = QIcon(
                            QStringLiteral(":/qwindowkit/widgets/windows/caption/maximize.svg"));
                        m_checkedIcon = QIcon(
                            QStringLiteral(":/qwindowkit/widgets/windows/caption/restore.svg"));
                        setAccessibleName(
                            QCoreApplication::translate("QWK::WindowsCaptionButton", "Maximize"));
                        break;
                    case CaptionButtonRole::Close:
                        setObjectName(QStringLiteral("qwkWindowsCloseButton"));
                        m_icon =
                            QIcon(QStringLiteral(":/qwindowkit/widgets/windows/caption/close.svg"));
                        setAccessibleName(
                            QCoreApplication::translate("QWK::WindowsCaptionButton", "Close"));
                        break;
                }
            }

            void setMaximized(bool maximized) {
                if (m_maximized == maximized) {
                    return;
                }
                m_maximized = maximized;
                setAccessibleName(
                    maximized
                        ? QCoreApplication::translate("QWK::WindowsCaptionButton", "Restore Down")
                        : QCoreApplication::translate("QWK::WindowsCaptionButton", "Maximize"));
                update();
            }

        protected:
            void paintEvent(QPaintEvent *event) override {
                Q_UNUSED(event)

                QPainter painter(this);
                const bool hovered = underMouse();
                const bool pressed = isDown();
                const bool closeButton = m_role == CaptionButtonRole::Close;

                if (hovered || pressed) {
                    QColor background;
                    if (closeButton) {
                        background = pressed ? QColor(196, 43, 28) : QColor(232, 17, 35);
                    } else {
                        const bool dark = palette().window().color().lightness() < 128;
                        background = dark ? QColor(255, 255, 255, pressed ? 36 : 24)
                                          : QColor(0, 0, 0, pressed ? 34 : 20);
                    }
                    // Caption backplates are full-bleed. DWM owns the top-level corner clip;
                    // duplicating it here leaves an antialiased seam at fractional scale factors.
                    painter.fillRect(rect(), background);
                }

                QColor glyphColor = palette().windowText().color();
                if (closeButton && (hovered || pressed)) {
                    glyphColor = Qt::white;
                } else if (!isEnabled()) {
                    glyphColor.setAlpha(90);
                } else if (window() && !window()->isActiveWindow()) {
                    glyphColor.setAlpha(150);
                }

                const QIcon &icon = m_maximized && !m_checkedIcon.isNull() ? m_checkedIcon : m_icon;
                QPixmap glyph = icon.pixmap(iconSize());
                if (!glyph.isNull()) {
                    QPainter glyphPainter(&glyph);
                    glyphPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                    glyphPainter.fillRect(glyph.rect(), glyphColor);
                    glyphPainter.end();

                    const qreal dpr = glyph.devicePixelRatio();
                    const QSize logicalSize(qRound(glyph.width() / dpr),
                                            qRound(glyph.height() / dpr));
                    const QPoint origin((width() - logicalSize.width()) / 2,
                                        (height() - logicalSize.height()) / 2);
                    painter.drawPixmap(origin, glyph);
                }
            }

        private:
            CaptionButtonRole m_role;
            QIcon m_icon;
            QIcon m_checkedIcon;
            bool m_maximized = false;
        };

        class WindowsCaptionButtonBar final : public QWidget {
        public:
            explicit WindowsCaptionButtonBar(QWidget *host) : QWidget(host), m_host(host) {
                setObjectName(QStringLiteral("qwkWindowsCaptionButtonBar"));
                setAttribute(Qt::WA_StyledBackground, false);
                setFixedHeight(kCaptionButtonHeight);

                auto *layout = new QHBoxLayout(this);
                layout->setContentsMargins(0, 0, 0, 0);
                layout->setSpacing(0);

                m_minimizeButton = new WindowsCaptionButton(CaptionButtonRole::Minimize, this);
                m_maximizeButton = new WindowsCaptionButton(CaptionButtonRole::Maximize, this);
                m_closeButton = new WindowsCaptionButton(CaptionButtonRole::Close, this);

                layout->addWidget(m_minimizeButton);
                layout->addWidget(m_maximizeButton);
                layout->addWidget(m_closeButton);

                host->installEventFilter(this);
                updateFromHost();
            }

            WindowsCaptionButton *minimizeButton() const {
                return m_minimizeButton;
            }
            WindowsCaptionButton *maximizeButton() const {
                return m_maximizeButton;
            }
            WindowsCaptionButton *closeButton() const {
                return m_closeButton;
            }

            void updateFromHost() {
                if (!m_host) {
                    return;
                }

                const Qt::WindowFlags flags = m_host->windowFlags();
                const bool canMinimize = flags.testFlag(Qt::WindowMinimizeButtonHint);
                const bool canMaximize = flags.testFlag(Qt::WindowMaximizeButtonHint) &&
                                         m_host->minimumSize() != m_host->maximumSize();
                const bool canClose = flags.testFlag(Qt::WindowCloseButtonHint);
                m_minimizeButton->setEnabled(canMinimize);
                m_maximizeButton->setEnabled(canMaximize);
                m_closeButton->setEnabled(canClose);
                if (!canMinimize) {
                    m_minimizeButton->hide();
                }
                if (!canMaximize) {
                    m_maximizeButton->hide();
                }
                if (!canClose) {
                    m_closeButton->hide();
                }

                const int visibleButtonCount =
                    (canMinimize ? 1 : 0) + (canMaximize ? 1 : 0) + (canClose ? 1 : 0);
                setFixedWidth(kCaptionButtonWidth * visibleButtonCount);
                move(std::max(0, m_host->width() - width()), 0);
                m_maximizeButton->setMaximized(m_host->isMaximized());
                ensureRaised();
            }

        protected:
            bool event(QEvent *event) override {
                if (!m_raising && (event->type() == QEvent::ZOrderChange ||
                                   event->type() == QEvent::ShowToParent)) {
                    ensureRaised();
                }
                return QWidget::event(event);
            }

            bool eventFilter(QObject *watched, QEvent *event) override {
                if (watched != m_host) {
                    return false;
                }

                switch (event->type()) {
                    case QEvent::Show:
                    case QEvent::Resize:
                    case QEvent::LayoutRequest:
                    case QEvent::ChildAdded:
                    case QEvent::WindowStateChange:
                    case QEvent::WindowActivate:
                    case QEvent::WindowDeactivate:
                    case QEvent::PaletteChange:
                        updateFromHost();
                        update();
                        break;
                    case QEvent::UpdateRequest:
                        // A child update is coalesced into the host's UpdateRequest. Updating the
                        // caption bar here would post another host request and create an endless
                        // repaint loop while the window is otherwise idle.
                        break;
                    default:
                        break;
                }
                return false;
            }

        private:
            void ensureRaised() {
                if (!m_host) {
                    return;
                }
                m_raising = true;
                raise();
                m_raising = false;
                update();
                if (m_raiseQueued) {
                    return;
                }
                m_raiseQueued = true;
                QTimer::singleShot(0, this, [this]() {
                    m_raiseQueued = false;
                    if (!m_host) {
                        return;
                    }
                    // The application title bar can be raised during the same
                    // resize/layout pass. Run once after that pass so hover
                    // painting is never hidden behind custom chrome widgets.
                    m_raising = true;
                    raise();
                    m_raising = false;
                    update();
                });
            }

            QPointer<QWidget> m_host;
            WindowsCaptionButton *m_minimizeButton = nullptr;
            WindowsCaptionButton *m_maximizeButton = nullptr;
            WindowsCaptionButton *m_closeButton = nullptr;
            bool m_raiseQueued = false;
            bool m_raising = false;
        };

    }

    bool WidgetWindowAgentPrivate::installPlatformSystemButtons() {
        if (!hostWidget) {
            return false;
        }
        if (windowsSystemButtonBar) {
            return true;
        }

        initializeWindowsCaptionButtonResources();
        auto *bar = new WindowsCaptionButtonBar(hostWidget);
        windowsSystemButtonBar = bar;

        Q_Q(WidgetWindowAgent);
        q->setSystemButton(WindowAgentBase::Minimize, bar->minimizeButton());
        q->setSystemButton(WindowAgentBase::Maximize, bar->maximizeButton());
        q->setSystemButton(WindowAgentBase::Close, bar->closeButton());

        QObject::connect(
            bar->minimizeButton(), &QPushButton::clicked, hostWidget,
            [host = QPointer<QWidget>(hostWidget)]() { postSystemCommand(host, SC_MINIMIZE); });
        QObject::connect(bar->maximizeButton(), &QPushButton::clicked, hostWidget,
                         [host = QPointer<QWidget>(hostWidget), button = bar->maximizeButton()]() {
                             if (!host) {
                                 return;
                             }
                             postSystemCommand(host,
                                               host->isMaximized() ? SC_RESTORE : SC_MAXIMIZE);
                             QCoreApplication::postEvent(button, new QEvent(QEvent::Leave));
                         });
        QObject::connect(
            bar->closeButton(), &QPushButton::clicked, hostWidget,
            [host = QPointer<QWidget>(hostWidget)]() { postSystemCommand(host, SC_CLOSE); });

        bar->show();
        bar->updateFromHost();
        return true;
    }

    QRect WidgetWindowAgentPrivate::platformSystemButtonAreaGeometry() const {
        return windowsSystemButtonBar ? windowsSystemButtonBar->geometry() : QRect();
    }

#if QWINDOWKIT_CONFIG(ENABLE_WINDOWS_SYSTEM_BORDERS)
    // https://github.com/qt/qtbase/blob/e26a87f1ecc40bc8c6aa5b889fce67410a57a702/src/plugins/platforms/windows/qwindowsbackingstore.cpp#L42
    // In QtWidgets applications, when repainting happens, QPA at the last calls
    // QWindowsBackingStore::flush() to draw the contents of the buffer to the screen, we need to
    // call GDI drawing the top border after that.

    // After debugging, we know that there are two situations that will lead to repaint.
    //
    // 1. Windows sends a WM_PAINT message, after which Qt immediately generates a QExposeEvent or
    // QResizeEvent and send it to the corresponding QWidgetWindow instance, calling "flush" at the
    // end of its handler.
    //
    // 2. When a timer or user input triggers Qt to repaint spontaneously, the corresponding
    // QWidget receives a QEvent::UpdateRequest event and also calls "flush" at the end of its
    // handler.
    //
    // The above two cases are mutually exclusive, so we just need to intercept the two events
    // separately and draw the border area after the "flush" is called.

    // https://github.com/qt/qtbase/blob/e26a87f1ecc40bc8c6aa5b889fce67410a57a702/src/plugins/platforms/windows/qwindowswindow.cpp#L2440
    // Note that we can not draw the border right after WM_PAINT comes or right before the WndProc
    // returns, because Qt calls BeginPaint() and EndPaint() itself. We should make sure that we
    // draw the top border between these two calls, otherwise some display exceptions may arise.

    class WidgetBorderHandler : public QObject, public Windows10BorderHandler {
    public:
        explicit WidgetBorderHandler(QWidget *widget, AbstractWindowContext *ctx,
                                     QObject *parent = nullptr)
            : QObject(parent), Windows10BorderHandler(ctx), widget(widget) {
            widget->installEventFilter(this);

            // First update
            if (ctx->windowId()) {
                setupNecessaryAttributes();
            }
            WidgetBorderHandler::updateGeometry();
        }

        void updateGeometry() override {
            // The window top border is manually painted by QWK so we want to give
            // some margins to avoid it covering real window contents, however, we
            // found that there are some rounding issues for the thin border and
            // thus this small trick doesn't work very well when the DPR is not
            // integer. So far we haven't found a perfect solution, so just don't
            // set any margins. In theory the window content will only be covered
            // by 1px or so, it should not be a serious issue in the real world.
            //
            // widget->setContentsMargins(isNormalWindow() ? QMargins(0, borderThickness(), 0, 0)
            //                                             : QMargins());
        }

        bool isWindowActive() const override {
            return widget->isActiveWindow();
        }

        inline void forwardEventToWidgetAndDraw(QWidget *w, QEvent *event) {
            // https://github.com/qt/qtbase/blob/e26a87f1ecc40bc8c6aa5b889fce67410a57a702/src/widgets/kernel/qapplication.cpp#L3286
            // Deliver the event
            if (!forwardObjectEventFilters(this, w, event)) {
                // Let the widget paint first
                std::ignore = static_cast<QObject *>(w)->event(event);
                QCoreApplicationPrivate::setEventSpontaneous(event, false);
            }

            // Due to the timer or user action, Qt will repaint some regions spontaneously,
            // even if there is no WM_PAINT message, we must wait for it to finish painting
            // and then update the top border area.
            drawBorderNative();
        }

        inline void forwardEventToWindowAndDraw(QWindow *window, QEvent *event) {
            // https://github.com/qt/qtbase/blob/e26a87f1ecc40bc8c6aa5b889fce67410a57a702/src/widgets/kernel/qapplication.cpp#L3286
            // Deliver the event
            if (!forwardObjectEventFilters(ctx, window, event)) {
                // Let Qt paint first
                std::ignore = static_cast<QObject *>(window)->event(event);
                QCoreApplicationPrivate::setEventSpontaneous(event, false);
            }

            // Upon receiving the WM_PAINT message, Qt will repaint the entire view, and we
            // must wait for it to finish painting before drawing this top border area.
            drawBorderNative();
        }

    protected:
        bool sharedEventFilter(QObject *obj, QEvent *event) override {
            Q_UNUSED(obj)

            switch (event->type()) {
                case QEvent::Expose: {
                    // Qt will absolutely send a QExposeEvent or QResizeEvent to the QWindow when it
                    // receives a WM_PAINT message. When the control flow enters the expose handler,
                    // Qt must have already called BeginPaint() and it's the best time for us to
                    // draw the top border.

                    // Since a QExposeEvent will be sent immediately after the QResizeEvent, we can
                    // simply ignore it.
#  if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
                    struct ExposeEvent : public QExposeEvent {
                        inline const QRegion &getRegion() const {
                            return m_region;
                        }
                    };
                    auto ee = static_cast<ExposeEvent *>(event);
                    bool exposeRegionValid = !ee->getRegion().isNull();
#  else
                    auto ee = static_cast<QExposeEvent *>(event);
                    bool exposeRegionValid = !ee->region().isNull();
#  endif
                    auto window = widget->windowHandle();
                    if (window->isExposed() && isNormalWindow() && exposeRegionValid) {
                        forwardEventToWindowAndDraw(window, event);
                        return true;
                    }
                    break;
                }
                default:
                    break;
            }
            return Windows10BorderHandler::sharedEventFilter(obj, event);
        }

        bool eventFilter(QObject *obj, QEvent *event) override {
            Q_UNUSED(obj)

            switch (event->type()) {
                case QEvent::UpdateRequest: {
                    if (!isNormalWindow())
                        break;
                    forwardEventToWidgetAndDraw(widget, event);
                    return true;
                }

                case QEvent::WindowStateChange: {
                    updateGeometry();
                    break;
                }

                case QEvent::WindowActivate:
                case QEvent::WindowDeactivate: {
                    widget->update();
                    break;
                }

                default:
                    break;
            }
            return false;
        }

        QWidget *widget;
    };

    void WidgetWindowAgentPrivate::setupWindows10BorderWorkaround() {
        // Install painting hook
        auto ctx = context.get();
        if (ctx->windowAttribute(QStringLiteral("win10-border-needed")).toBool()) {
            borderHandler = std::make_unique<WidgetBorderHandler>(hostWidget, ctx);
        }
    }
#endif

}
