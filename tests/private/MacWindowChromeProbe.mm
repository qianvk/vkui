// SPDX-License-Identifier: MIT

#include <QRectF>
#include <QtGui/qwindowdefs.h>
#include <cmath>

#import <AppKit/AppKit.h>

bool macWindowUsesNativeFullScreen(const WId nativeViewId) {
    NSView* view = reinterpret_cast<NSView*>(nativeViewId);
    NSWindow* window = view != nil ? view.window : nil;
    return window != nil &&
           (window.styleMask & NSWindowStyleMaskFullScreen) != 0;
}

bool macToggleNativeFullScreen(const WId nativeViewId) {
    NSView* view = reinterpret_cast<NSView*>(nativeViewId);
    NSWindow* window = view != nil ? view.window : nil;
    if (window == nil) {
        return false;
    }
    [window toggleFullScreen:nil];
    return true;
}

bool macPerformNativeClose(const WId nativeViewId) {
    NSView* view = reinterpret_cast<NSView*>(nativeViewId);
    NSWindow* window = view != nil ? view.window : nil;
    if (window == nil) {
        return false;
    }
    [window performClose:nil];
    return true;
}

bool macTrafficLightsAreHidden(const WId nativeViewId) {
    NSView* view = reinterpret_cast<NSView*>(nativeViewId);
    NSWindow* window = view != nil ? view.window : nil;
    if (window == nil) {
        return false;
    }

    const NSWindowButton buttonTypes[] = {
        NSWindowCloseButton,
        NSWindowMiniaturizeButton,
        NSWindowZoomButton,
    };
    for (const NSWindowButton type : buttonTypes) {
        NSButton* button = [window standardWindowButton:type];
        if (button != nil && !button.hidden) {
            return false;
        }
    }
    return true;
}

int macVisibleSystemButtons(const WId nativeViewId) {
    NSView* view = reinterpret_cast<NSView*>(nativeViewId);
    NSWindow* window = view != nil ? view.window : nil;
    if (window == nil) {
        return 0;
    }
    NSButton* close = [window standardWindowButton:NSWindowCloseButton];
    NSButton* minimize = [window standardWindowButton:NSWindowMiniaturizeButton];
    NSButton* maximize = [window standardWindowButton:NSWindowZoomButton];
    int buttons = 0;
    if (close != nil && !close.hidden) {
        buttons |= 0x01;
    }
    if (minimize != nil && !minimize.hidden) {
        buttons |= 0x02;
    }
    if (maximize != nil && !maximize.hidden) {
        buttons |= 0x04;
    }
    return buttons;
}

bool macForceTrafficLightsVisible(const WId nativeViewId) {
    NSView* view = reinterpret_cast<NSView*>(nativeViewId);
    NSWindow* window = view != nil ? view.window : nil;
    if (window == nil) {
        return false;
    }

    bool hasButton = false;
    const NSWindowButton buttonTypes[] = {
        NSWindowCloseButton,
        NSWindowMiniaturizeButton,
        NSWindowZoomButton,
    };
    for (const NSWindowButton type : buttonTypes) {
        NSButton* button = [window standardWindowButton:type];
        if (button != nil) {
            hasButton = true;
            button.hidden = NO;
        }
    }
    return hasButton;
}

QRectF macTrafficLightGeometry(const WId nativeViewId) {
    NSView* view = reinterpret_cast<NSView*>(nativeViewId);
    NSWindow* window = view != nil ? view.window : nil;
    if (window == nil) {
        return {};
    }

    NSRect buttonGroup = NSZeroRect;
    bool hasButton = false;
    const NSWindowButton buttonTypes[] = {
        NSWindowCloseButton,
        NSWindowMiniaturizeButton,
        NSWindowZoomButton,
    };
    for (const NSWindowButton type : buttonTypes) {
        NSButton* button = [window standardWindowButton:type];
        if (button == nil || button.superview == nil || button.hidden) {
            continue;
        }
        const NSRect rect = [button.superview convertRect:button.frame toView:view];
        buttonGroup = hasButton ? NSUnionRect(buttonGroup, rect) : rect;
        hasButton = true;
    }
    if (!hasButton) {
        return {};
    }

    const CGFloat y =
        view.flipped ? NSMinY(buttonGroup) : view.frame.size.height - NSMaxY(buttonGroup);
    return {NSMinX(buttonGroup), y, NSWidth(buttonGroup), NSHeight(buttonGroup)};
}

bool macTrafficLightsMatchGeometry(const WId nativeViewId, const QRectF& expected) {
    const QRectF actual = macTrafficLightGeometry(nativeViewId);
    constexpr qreal tolerance = 1.0;
    return actual.isValid() && expected.isValid() &&
           std::abs(actual.x() - expected.x()) <= tolerance &&
           std::abs(actual.y() - expected.y()) <= tolerance &&
           std::abs(actual.width() - expected.width()) <= tolerance &&
           std::abs(actual.height() - expected.height()) <= tolerance;
}

bool macTrafficLightsFitInNativeTitleBar(const WId nativeViewId) {
    NSView* view = reinterpret_cast<NSView*>(nativeViewId);
    NSWindow* window = view != nil ? view.window : nil;
    if (window == nil) {
        return false;
    }

    const NSWindowButton buttonTypes[] = {
        NSWindowCloseButton,
        NSWindowMiniaturizeButton,
        NSWindowZoomButton,
    };
    for (const NSWindowButton type : buttonTypes) {
        NSButton* button = [window standardWindowButton:type];
        if (button == nil || button.superview == nil || button.hidden) {
            continue;
        }
        if (!NSContainsRect(button.superview.bounds, button.frame)) {
            return false;
        }
    }
    return true;
}
