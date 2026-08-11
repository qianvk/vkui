// SPDX-License-Identifier: MIT

#include "MacWindowStackProbe.h"

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>


bool vkuiNativeWindowIsInFrontOf(const WId candidate, const WId reference) {
    NSView* candidateView = reinterpret_cast<NSView*>(candidate);
    NSView* referenceView = reinterpret_cast<NSView*>(reference);
    NSWindow* candidateWindow = candidateView.window;
    NSWindow* referenceWindow = referenceView.window;
    if (!candidateWindow || !referenceWindow) {
        return false;
    }

    const CGWindowID candidateNumber = static_cast<CGWindowID>(candidateWindow.windowNumber);
    const CGWindowID referenceNumber = static_cast<CGWindowID>(referenceWindow.windowNumber);
    CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionAll, kCGNullWindowID);
    if (!windows) {
        return false;
    }

    CFIndex candidateIndex = -1;
    CFIndex referenceIndex = -1;
    const CFIndex count = CFArrayGetCount(windows);
    for (CFIndex index = 0; index < count; ++index) {
        const auto* description = static_cast<CFDictionaryRef>(
            CFArrayGetValueAtIndex(windows, index));
        const auto* number = static_cast<CFNumberRef>(
            CFDictionaryGetValue(description, kCGWindowNumber));
        CGWindowID windowNumber = 0;
        if (!number || !CFNumberGetValue(number, kCFNumberSInt32Type, &windowNumber)) {
            continue;
        }
        if (windowNumber == candidateNumber) {
            candidateIndex = index;
        } else if (windowNumber == referenceNumber) {
            referenceIndex = index;
        }
    }
    CFRelease(windows);
    return candidateIndex >= 0 && referenceIndex >= 0 && candidateIndex < referenceIndex;
}
