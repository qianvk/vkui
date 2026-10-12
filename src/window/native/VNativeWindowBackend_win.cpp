// SPDX-License-Identifier: MIT

#include "VNativeWindowController_p.h"
#include "VPlatformWindowBackend_p.h"

#include <QAbstractNativeEventFilter>
#include <QGuiApplication>
#include <QPainter>
#include <QPointer>
#include <QWidget>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

// Common controls require the Win32 declarations above.
#include <CommCtrl.h>
#include <dwmapi.h>
#include <windowsx.h>

namespace vkui::windowing {
namespace {

constexpr DWORD kDwmWindowCornerPreference = DWMWA_WINDOW_CORNER_PREFERENCE;
constexpr DWORD kDwmWindowCornerRound = DWMWCP_ROUND;

// Expose DWM's surface through the QWidget backing store. This paints no caption
// glyphs or hover states: Windows owns all of those pixels and interactions.
class NativeCaptionSurface final : public QWidget {
  public:
    explicit NativeCaptionSurface(QWidget* parent) : QWidget(parent) {
        setObjectName(QStringLiteral("VkNativeCaptionSurface"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
    }
};

} // namespace

class WindowsWindowBackend final : public VPlatformWindowBackend,
                                   public QAbstractNativeEventFilter {
  public:
    explicit WindowsWindowBackend(VNativeWindowController& controller)
        : VPlatformWindowBackend(controller) {}

    ~WindowsWindowBackend() override {
        detach();
    }

    bool attach(QWidget*, QWindow*, const WId nativeId) override {
        detach();
        if (!QGuiApplication::platformName().startsWith(QStringLiteral("windows"),
                                                        Qt::CaseInsensitive)) {
            return false;
        }
        hwnd_ = reinterpret_cast<HWND>(nativeId);
        if (hwnd_ == nullptr || !IsWindow(hwnd_)) {
            hwnd_ = nullptr;
            return false;
        }
        subclassId_ = reinterpret_cast<UINT_PTR>(this);
        if (!SetWindowSubclass(hwnd_, windowProcedure, subclassId_,
                               reinterpret_cast<DWORD_PTR>(this))) {
            hwnd_ = nullptr;
            return false;
        }
        QCoreApplication::instance()->installNativeEventFilter(this);
        const DWORD cornerPreference = kDwmWindowCornerRound;
        static_cast<void>(DwmSetWindowAttribute(hwnd_, kDwmWindowCornerPreference,
                                                &cornerPreference, sizeof(cornerPreference)));
        applyFrameStyle();
        // Run the first calculation through Qt even when no style bits changed. Qt records
        // the resulting margins in its platform window before the host is shown.
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        return true;
    }

    void detach() noexcept override {
        delete captionSurface_.data();
        if (QCoreApplication::instance()) {
            QCoreApplication::instance()->removeNativeEventFilter(this);
        }
        if (hwnd_ == nullptr) {
            return;
        }
        const HWND window = hwnd_;
        RemoveWindowSubclass(window, windowProcedure, subclassId_);
        hwnd_ = nullptr;
        subclassId_ = 0;
    }

    void synchronize() override {
        if (hwnd_ == nullptr) {
            return;
        }
        applyFrameStyle();
    }

    void configureSystemButtons() override {
        const VSystemButtons configuredButtons = controller_.systemButtons();
        closeEnabled_ = configuredButtons.testFlag(VSystemButton::Close);
        minimizeEnabled_ = configuredButtons.testFlag(VSystemButton::Minimize);
        maximizeEnabled_ = configuredButtons.testFlag(VSystemButton::Maximize);
    }

    void updateSystemButtonLayout() override {
        // DWM owns caption-button geometry on Windows. Keeping that geometry is
        // required for Snap Layout and native accessibility behavior.
        updateCaptionSurface();
    }

    void applySystemButtonState() override {
        applyFrameStyle();
    }

    void updateResizablePolicy() override {
        applyFrameStyle();
    }

    void startSystemMove(const QPoint&) override {
        if (hwnd_ == nullptr) {
            return;
        }
        POINT cursor{};
        if (!GetCursorPos(&cursor)) {
            return;
        }
        ReleaseCapture();
        SendMessageW(hwnd_, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(cursor.x, cursor.y));
    }

    void handleTitleBarDoubleClick(const QPoint&) override {
        POINT cursor{};
        if (hwnd_ != nullptr && GetCursorPos(&cursor)) {
            SendMessageW(hwnd_, WM_NCLBUTTONDBLCLK, HTCAPTION, MAKELPARAM(cursor.x, cursor.y));
        }
    }

    void showSystemMenu(const QPoint&) override {
        if (hwnd_ == nullptr) {
            return;
        }
        HMENU menu = GetSystemMenu(hwnd_, FALSE);
        if (menu == nullptr) {
            return;
        }
        POINT cursor{};
        if (!GetCursorPos(&cursor)) {
            return;
        }
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x,
                                            cursor.y, 0, hwnd_, nullptr);
        if (command != 0) {
            PostMessageW(hwnd_, WM_SYSCOMMAND, command, 0);
        }
    }

    void raiseWindow() override {
        if (hwnd_ == nullptr) {
            return;
        }
        if (IsIconic(hwnd_)) {
            ShowWindow(hwnd_, SW_RESTORE);
        } else {
            ShowWindow(hwnd_, SW_SHOW);
        }
        BringWindowToTop(hwnd_);
        SetForegroundWindow(hwnd_);
    }

  private:
    QColor captionBackground(const QWidget* host) const {
        const QPoint captionPoint(host->width() - 20, 20);
        for (QWidget* titleBar : controller_.titleBars()) {
            if (!titleBar->isVisible() ||
                !QRect(titleBar->mapTo(host, QPoint()), titleBar->size()).contains(captionPoint)) {
                continue;
            }
            // Transparent app title bars inherit the surface painted by their
            // nearest opaque ancestor, which may use Base rather than Window.
            for (const QWidget* surface = titleBar; surface; surface = surface->parentWidget()) {
                if (surface->autoFillBackground()) {
                    return surface->palette().color(surface->backgroundRole());
                }
                if (surface == host) {
                    break;
                }
            }
        }
        return host->palette().color(QPalette::Window);
    }

    void updateCaptionSurface() {
        QWidget* host = controller_.host();
        if (!host || !hwnd_) {
            return;
        }
        const bool visible =
            controller_.systemButtonsVisible() && controller_.systemButtons() != VSystemButtons{};
        if (!visible) {
            if (captionSurface_) {
                captionSurface_->hide();
            }
            return;
        }
        RECT bounds{};
        RECT frame{};
        if (FAILED(DwmGetWindowAttribute(hwnd_, DWMWA_CAPTION_BUTTON_BOUNDS, &bounds,
                                         sizeof(bounds))) ||
            !GetWindowRect(hwnd_, &frame)) {
            return;
        }
        POINT origin{frame.left + bounds.left, frame.top + bounds.top};
        ScreenToClient(hwnd_, &origin);
        const qreal scale = devicePixelRatio();
        if (!captionSurface_) {
            captionSurface_ = new NativeCaptionSurface(host);
        }
        const int left = qRound(origin.x / scale);
        captionSurface_->setGeometry(left, qRound(origin.y / scale), qMax(0, host->width() - left),
                                     qCeil((bounds.bottom - bounds.top) / scale));
        captionSurface_->show();
        captionSurface_->raise();
        captionSurface_->update();
    }
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override {
        if (!message || !result || type != QByteArrayLiteral("windows_generic_MSG")) {
            return false;
        }
        const auto* msg = static_cast<const MSG*>(message);
        if (hwnd_ == nullptr || msg->hwnd != hwnd_ || msg->message != WM_NCCALCSIZE ||
            msg->wParam == FALSE) {
            return false;
        }
        // Run inside Qt's WndProc so Qt records the real full-client frame margins.
        auto* sizes = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
        if (IsZoomed(hwnd_)) {
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor)) {
                RECT client{};
                IntersectRect(&client, &sizes->rgrc[0], &monitor.rcWork);
                sizes->rgrc[0] = client;
            }
        } else if (!controller_.hostSizeFixed()) {
            // Preserve Windows' invisible side/bottom resize borders. Expanding
            // into them makes the visible client wider than DWM's caption strip.
            // Use the same metric with captions hidden, avoiding a client-size
            // change when Focus Mode or dialog policy removes WS_CAPTION.
            const int border = nativeResizeBorderThickness();
            sizes->rgrc[0].left += border;
            sizes->rgrc[0].right -= border;
            sizes->rgrc[0].bottom -= border;
        }
        *result = 0;
        return true;
    }
    static LRESULT CALLBACK windowProcedure(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR subclassId, DWORD_PTR referenceData) {
        Q_UNUSED(subclassId)
        auto* self = reinterpret_cast<WindowsWindowBackend*>(referenceData);
        if (self == nullptr) {
            return DefSubclassProc(hwnd, message, wParam, lParam);
        }
        if (message == WM_NCDESTROY) {
            const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
            self->hwnd_ = nullptr;
            return result;
        }
        return self->handleWindowMessage(hwnd, message, wParam, lParam);
    }

    LRESULT handleWindowMessage(HWND hwnd, const UINT message, const WPARAM wParam,
                                const LPARAM lParam) {
        // DWM owns the caption glyphs, hover/press states and Snap Layouts. In particular
        // WM_NCMOUSEMOVE/LEAVE must reach DWM, not just the hit-test query.
        LRESULT dwmResult = 0;
        if (message != WM_NCCALCSIZE &&
            DwmDefWindowProc(hwnd, message, wParam, lParam, &dwmResult) &&
            (message != WM_NCHITTEST || (dwmResult != HTCLIENT && dwmResult != HTNOWHERE))) {
            return dwmResult;
        }
        switch (message) {
        case WM_NCHITTEST: {
            // DWM can decline hit tests on maximized custom frames. Ask USER32
            // for the native accessible button rectangles; never infer/draw controls.
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT client = point;
            ScreenToClient(hwnd, &client);
            const qreal scale = devicePixelRatio();
            const QPoint logical(qRound(client.x / scale), qRound(client.y / scale));
            if (captionSurface_ && captionSurface_->isVisible() &&
                captionSurface_->geometry().contains(logical)) {
                TITLEBARINFOEX titleBar{sizeof(TITLEBARINFOEX)};
                SendMessageW(hwnd, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&titleBar));
                for (const auto [index, hit] : {std::pair{2, HTMINBUTTON},
                                                std::pair{3, HTMAXBUTTON}, std::pair{5, HTCLOSE}}) {
                    if (!(titleBar.rgstate[index] &
                          (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_UNAVAILABLE)) &&
                        PtInRect(&titleBar.rgrect[index], point)) {
                        return hit;
                    }
                }
            }
            return hitTest(QPoint(point.x, point.y), logical);
        }
        case WM_SIZE:
            controller_.synchronizeNativeWindow();
            break;
        case WM_DPICHANGED:
        case WM_DWMCOMPOSITIONCHANGED:
            controller_.synchronizeNativeWindow();
            break;
        default:
            break;
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    LRESULT hitTest(const QPoint& screenPosition, const QPoint& logicalPosition) const {
        if (hwnd_ == nullptr) {
            return HTNOWHERE;
        }
        RECT windowRect{};
        if (!GetWindowRect(hwnd_, &windowRect)) {
            return HTNOWHERE;
        }
        const int border =
            controller_.hostSizeFixed() || IsZoomed(hwnd_) ? 0 : nativeResizeBorderThickness();
        const bool left = border > 0 && screenPosition.x() < windowRect.left + border;
        const bool right = border > 0 && screenPosition.x() >= windowRect.right - border;
        const bool top = border > 0 && screenPosition.y() < windowRect.top + border;
        const bool bottom = border > 0 && screenPosition.y() >= windowRect.bottom - border;
        if (top && left) {
            return HTTOPLEFT;
        }
        if (top && right) {
            return HTTOPRIGHT;
        }
        if (bottom && left) {
            return HTBOTTOMLEFT;
        }
        if (bottom && right) {
            return HTBOTTOMRIGHT;
        }
        if (left) {
            return HTLEFT;
        }
        if (right) {
            return HTRIGHT;
        }
        if (top) {
            return HTTOP;
        }
        if (bottom) {
            return HTBOTTOM;
        }

        return controller_.pointIsDraggable(logicalPosition) ? HTCAPTION : HTCLIENT;
    }

    int dpi() const {
        constexpr int kDefaultDpi = 96;
        return hwnd_ != nullptr ? static_cast<int>(GetDpiForWindow(hwnd_)) : kDefaultDpi;
    }

    qreal devicePixelRatio() const {
        if (QWindow* window = controller_.window()) {
            return std::max<qreal>(1.0, window->devicePixelRatio());
        }
        constexpr qreal kDefaultDpi = 96.0;
        return std::max<qreal>(1.0, dpi() / kDefaultDpi);
    }

    int nativeResizeBorderThickness() const {
        const UINT windowDpi = static_cast<UINT>(std::max(1, dpi()));
        return GetSystemMetricsForDpi(SM_CXSIZEFRAME, windowDpi) +
               GetSystemMetricsForDpi(SM_CXPADDEDBORDER, windowDpi);
    }

    int nativeTitleBarHeight() const {
        const UINT windowDpi = static_cast<UINT>(std::max(1, dpi()));
        return GetSystemMetricsForDpi(SM_CYCAPTION, windowDpi) + nativeResizeBorderThickness();
    }

    void applyFrameStyle() {
        if (hwnd_ == nullptr || applyingFrameStyle_) {
            return;
        }
        applyingFrameStyle_ = true;
        const bool buttonsVisible =
            controller_.systemButtonsVisible() && controller_.systemButtons() != VSystemButtons{};
        bool frameChanged = false;
        if (QWindow* window = controller_.window()) {
            auto flags = window->flags();
            flags.setFlag(Qt::CustomizeWindowHint, true);
            flags.setFlag(Qt::WindowTitleHint, true);
            flags.setFlag(Qt::WindowSystemMenuHint, false);
            flags.setFlag(Qt::WindowCloseButtonHint, buttonsVisible && closeEnabled_);
            flags.setFlag(Qt::WindowMinimizeButtonHint, buttonsVisible && minimizeEnabled_);
            flags.setFlag(Qt::WindowMaximizeButtonHint,
                          buttonsVisible && maximizeEnabled_ && !controller_.hostSizeFixed());
            flags.setFlag(Qt::MSWindowsFixedSizeDialogHint, controller_.hostSizeFixed());
            if (flags != window->flags()) {
                // QWindow avoids QWidget's normalization that restores WindowTitleHint for
                // caption buttons. Keep transient Win32 state across Qt's style update as well.
                constexpr LONG_PTR stateMask = WS_VISIBLE | WS_DISABLED | WS_MINIMIZE | WS_MAXIMIZE;
                const LONG_PTR state = GetWindowLongPtrW(hwnd_, GWL_STYLE) & stateMask;
                window->setFlags(flags);
                frameChanged = true;
                const LONG_PTR current = GetWindowLongPtrW(hwnd_, GWL_STYLE);
                const LONG_PTR next = (current & ~stateMask) | state;
                if (current != next) {
                    SetWindowLongPtrW(hwnd_, GWL_STYLE, next);
                }
            }
        }
        // Only replace capabilities owned by this backend. In particular never overwrite
        // WS_VISIBLE, WS_DISABLED, WS_MINIMIZE or WS_MAXIMIZE with a creation-time snapshot.
        constexpr LONG_PTR chromeMask =
            WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        const LONG_PTR current = GetWindowLongPtrW(hwnd_, GWL_STYLE);
        // Keep the native caption frame even without controls: Windows hides its
        // resize border outside the visible surface only for captioned windows.
        LONG_PTR chrome = WS_CAPTION | (controller_.hostSizeFixed() ? 0 : WS_THICKFRAME);
        if (buttonsVisible) {
            chrome |= WS_SYSMENU;
            chrome |= minimizeEnabled_ ? WS_MINIMIZEBOX : 0;
            chrome |= maximizeEnabled_ && !controller_.hostSizeFixed() ? WS_MAXIMIZEBOX : 0;
        }
        const LONG_PTR next = (current & ~chromeMask) | chrome;
        if (current != next) {
            SetWindowLongPtrW(hwnd_, GWL_STYLE, next);
            frameChanged = true;
        }
        if (frameChanged) {
            SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                             SWP_NOACTIVATE);
        }
        if (HMENU menu = GetSystemMenu(hwnd_, FALSE)) {
            EnableMenuItem(menu, SC_CLOSE, MF_BYCOMMAND | (closeEnabled_ ? MF_ENABLED : MF_GRAYED));
        }
        // Qt's expanded-client painter is disabled. DWM draws native caption buttons on
        // this extended frame; client calculations still pass through Qt's geometry cache.
        const MARGINS margins{0, 0, nativeTitleBarHeight(), 0};
        static_cast<void>(DwmExtendFrameIntoClientArea(hwnd_, &margins));
        if (const QWidget* host = controller_.host()) {
            const QPalette palette = host->palette();
            const BOOL dark = palette.color(QPalette::Window).lightness() <
                              palette.color(QPalette::WindowText).lightness();
            static_cast<void>(
                DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark)));
            const QColor background = captionBackground(host);
            const COLORREF captionColor =
                RGB(background.red(), background.green(), background.blue());
            static_cast<void>(DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &captionColor,
                                                    sizeof(captionColor)));
        }
        updateCaptionSurface();
        applyingFrameStyle_ = false;
    }

    HWND hwnd_ = nullptr;
    QPointer<NativeCaptionSurface> captionSurface_;
    UINT_PTR subclassId_ = 0;
    bool closeEnabled_ = true;
    bool minimizeEnabled_ = true;
    bool maximizeEnabled_ = true;
    bool applyingFrameStyle_ = false;
};

std::unique_ptr<VPlatformWindowBackend>
createPlatformWindowBackend(VNativeWindowController& controller) {
    return std::make_unique<WindowsWindowBackend>(controller);
}

} // namespace vkui::windowing
