// SPDX-License-Identifier: MIT

#include "VNativeWindowController_p.h"
#include "VPlatformWindowBackend_p.h"

#include <QGuiApplication>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <memory>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <CommCtrl.h>
#include <Windows.h>
#include <dwmapi.h>
#include <windowsx.h>

namespace vkui::windowing {
namespace {

constexpr DWORD kDwmWindowCornerPreference = 33;
constexpr DWORD kDwmWindowCornerRound = 2;

} // namespace

class WindowsWindowBackend final : public VPlatformWindowBackend {
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
        originalStyle_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);
        subclassId_ = reinterpret_cast<UINT_PTR>(this);
        if (!SetWindowSubclass(hwnd_, windowProcedure, subclassId_,
                               reinterpret_cast<DWORD_PTR>(this))) {
            hwnd_ = nullptr;
            return false;
        }
        const DWORD cornerPreference = kDwmWindowCornerRound;
        static_cast<void>(DwmSetWindowAttribute(hwnd_, kDwmWindowCornerPreference,
                                                &cornerPreference, sizeof(cornerPreference)));
        applyFrameStyle();
        return true;
    }

    void detach() noexcept override {
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
        const MARGINS margins{0, 0, nativeTitleBarHeight(), 0};
        static_cast<void>(DwmExtendFrameIntoClientArea(hwnd_, &margins));
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
        switch (message) {
        case WM_NCCALCSIZE:
            if (wParam != FALSE) {
                auto* parameters = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
                const RECT proposed = parameters->rgrc[0];
                const LRESULT result = DefSubclassProc(hwnd, message, wParam, lParam);
                parameters->rgrc[0].top =
                    proposed.top + (IsZoomed(hwnd) ? nativeResizeBorderThickness() : 0);
                return result;
            }
            break;
        case WM_NCHITTEST: {
            LRESULT result = HTNOWHERE;
            if (DwmDefWindowProc(hwnd, message, wParam, lParam, &result) && result != HTCLIENT &&
                result != HTNOWHERE) {
                return result;
            }
            return hitTest(QPoint(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)));
        }
        case WM_SIZE:
        case WM_DPICHANGED:
        case WM_DWMCOMPOSITIONCHANGED:
            controller_.synchronizeNativeWindow();
            break;
        default:
            break;
        }
        return DefSubclassProc(hwnd, message, wParam, lParam);
    }

    LRESULT hitTest(const QPoint& screenPosition) const {
        if (hwnd_ == nullptr) {
            return HTNOWHERE;
        }
        RECT windowRect{};
        if (!GetWindowRect(hwnd_, &windowRect)) {
            return HTNOWHERE;
        }
        const int border = controller_.hostSizeFixed() ? 0 : nativeResizeBorderThickness();
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

        POINT clientPoint{screenPosition.x(), screenPosition.y()};
        ScreenToClient(hwnd_, &clientPoint);
        const qreal scale = devicePixelRatio();
        const QPoint logicalPosition(static_cast<int>(std::lround(clientPoint.x / scale)),
                                     static_cast<int>(std::lround(clientPoint.y / scale)));
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
        LONG_PTR style = originalStyle_;
        const bool buttonsVisible =
            controller_.systemButtonsVisible() && controller_.systemButtons() != VSystemButtons{};
        if (buttonsVisible) {
            style |= WS_CAPTION | WS_SYSMENU;
            style = minimizeEnabled_ ? (style | WS_MINIMIZEBOX) : (style & ~WS_MINIMIZEBOX);
            style = maximizeEnabled_ && !controller_.hostSizeFixed() ? (style | WS_MAXIMIZEBOX)
                                                                     : (style & ~WS_MAXIMIZEBOX);
            if (HMENU menu = GetSystemMenu(hwnd_, FALSE)) {
                EnableMenuItem(menu, SC_CLOSE,
                               MF_BYCOMMAND | (closeEnabled_ ? MF_ENABLED : MF_GRAYED));
            }
        } else {
            style &= ~(WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
        }
        style = controller_.hostSizeFixed() ? (style & ~WS_THICKFRAME) : (style | WS_THICKFRAME);

        if (GetWindowLongPtrW(hwnd_, GWL_STYLE) != style) {
            SetWindowLongPtrW(hwnd_, GWL_STYLE, style);
            SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                             SWP_NOACTIVATE);
        }
        const MARGINS margins{0, 0, nativeTitleBarHeight(), 0};
        static_cast<void>(DwmExtendFrameIntoClientArea(hwnd_, &margins));
        applyingFrameStyle_ = false;
    }

    HWND hwnd_ = nullptr;
    UINT_PTR subclassId_ = 0;
    LONG_PTR originalStyle_ = 0;
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
