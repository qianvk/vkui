// SPDX-License-Identifier: MIT

#include "VNativeWindowController_p.h"
#include "VPlatformWindowBackend_p.h"

#import <AppKit/AppKit.h>
#include <QGuiApplication>
#include <QWindow>
#include <QtMath>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>

namespace vkui::windowing {
class MacWindowBackend;
} // namespace vkui::windowing

@interface VKUINativeWindowObserver : NSObject {
  @private
    vkui::windowing::MacWindowBackend* backend_;
}

- (instancetype)initWithBackend:(vkui::windowing::MacWindowBackend*)backend;
- (void)windowGeometryChanged:(NSNotification*)notification;
- (void)windowFullScreenWillChange:(NSNotification*)notification;
- (void)windowFullScreenChanged:(NSNotification*)notification;

@end

@interface VKUINativeButtonObserver : NSObject {
  @private
    vkui::windowing::MacWindowBackend* backend_;
    NSMutableArray<NSButton*>* buttons_;
}

- (instancetype)initWithBackend:(vkui::windowing::MacWindowBackend*)backend;
- (void)attachToButtons:(NSArray<NSButton*>*)buttons;
- (void)detach;

@end

namespace vkui::windowing {
namespace {

constexpr std::size_t kNativeButtonCount = 3;

std::array<NSWindowButton, kNativeButtonCount> nativeButtonTypes() {
    return {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton};
}

VSystemButton systemButtonForNativeType(const NSWindowButton type) {
    switch (type) {
    case NSWindowCloseButton:
        return VSystemButton::Close;
    case NSWindowMiniaturizeButton:
        return VSystemButton::Minimize;
    case NSWindowZoomButton:
        return VSystemButton::Maximize;
    default:
        return VSystemButton::Close;
    }
}

} // namespace

class MacWindowBackend final : public VPlatformWindowBackend {
  public:
    explicit MacWindowBackend(VNativeWindowController& controller)
        : VPlatformWindowBackend(controller) {}

    ~MacWindowBackend() override {
        detach();
    }

    bool attach(QWidget*, QWindow*, const WId nativeId) override {
        detach();
        if (!QGuiApplication::platformName().startsWith(QStringLiteral("cocoa"),
                                                        Qt::CaseInsensitive)) {
            return false;
        }
        view_ = reinterpret_cast<NSView*>(nativeId);
        window_ = view_ != nil ? view_.window : nil;
        if (view_ == nil || window_ == nil) {
            view_ = nil;
            window_ = nil;
            return false;
        }

        lifetimeToken_ = std::make_shared<std::atomic_bool>(true);
        applyWindowStyle();
        observer_ = [[VKUINativeWindowObserver alloc] initWithBackend:this];
        buttonObserver_ = [[VKUINativeButtonObserver alloc] initWithBackend:this];
        NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
        [center addObserver:observer_
                   selector:@selector(windowGeometryChanged:)
                       name:NSWindowDidResizeNotification
                     object:window_];
        [center addObserver:observer_
                   selector:@selector(windowGeometryChanged:)
                       name:NSWindowDidUpdateNotification
                     object:window_];
        [center addObserver:observer_
                   selector:@selector(windowFullScreenWillChange:)
                       name:NSWindowWillEnterFullScreenNotification
                     object:window_];
        [center addObserver:observer_
                   selector:@selector(windowFullScreenWillChange:)
                       name:NSWindowWillExitFullScreenNotification
                     object:window_];
        [center addObserver:observer_
                   selector:@selector(windowFullScreenChanged:)
                       name:NSWindowDidEnterFullScreenNotification
                     object:window_];
        [center addObserver:observer_
                   selector:@selector(windowFullScreenChanged:)
                       name:NSWindowDidExitFullScreenNotification
                     object:window_];

        return true;
    }

    void detach() noexcept override {
        if (lifetimeToken_ != nullptr) {
            lifetimeToken_->store(false, std::memory_order_release);
            lifetimeToken_.reset();
        }
        ++exitSettlementGeneration_;
        releasePendingMouseDown();
        if (observer_ != nil) {
            [NSNotificationCenter.defaultCenter removeObserver:observer_];
            [observer_ release];
            observer_ = nil;
        }
        if (buttonObserver_ != nil) {
            [buttonObserver_ detach];
            [buttonObserver_ release];
            buttonObserver_ = nil;
        }
        view_ = nil;
        window_ = nil;
        fullScreenTransition_ = FullScreenTransition::None;
        hideButtonsDuringFullScreenExit_ = false;
        settlingFullScreenExit_ = false;
    }

    void synchronize() override {
        if (window_ == nil || synchronizing_) {
            return;
        }
        // AppKit owns the temporary title-bar hierarchy throughout a native
        // full-screen transition. Reapplying windowed state here causes
        // visible button jumps and can interfere with AppKit's animation.
        if (fullScreenTransition_ != FullScreenTransition::None) {
            return;
        }
        synchronizing_ = true;
        if (hideButtonsDuringFullScreenExit_) {
            // Hide newly reparented/recreated AppKit buttons before changing
            // their frame, otherwise the frame correction itself is visible.
            applySystemButtonState();
            updateSystemButtonLayout();
        } else {
            updateSystemButtonLayout();
            applySystemButtonState();
        }
        synchronizing_ = false;
    }

    void configureSystemButtons() override {
        const VSystemButtons configuredButtons = controller_.systemButtons();
        for (std::size_t index = 0; index < kNativeButtonCount; ++index) {
            enabledButtons_[index] =
                configuredButtons.testFlag(systemButtonForNativeType(nativeButtonTypes()[index]));
        }
    }

    void updateSystemButtonLayout() override {
        if (window_ == nil || view_ == nil) {
            return;
        }
        const auto buttons = systemButtons();
        std::array<NSButton*, kNativeButtonCount> activeButtons{nil, nil, nil};
        std::size_t activeCount = 0;
        for (std::size_t index = 0; index < buttons.size(); ++index) {
            if (buttons[index] != nil && enabledButtons_[index]) {
                activeButtons[activeCount++] = buttons[index];
            }
        }
        if (activeCount == 0 || activeButtons[0].superview == nil) {
            return;
        }

        // AppKit reparents and lays out the standard buttons during full-screen
        // transitions. Replaying windowed coordinates in that interval clips
        // the buttons against the transient title-bar container.
        if (usesNativeFullScreenButtonLayout()) {
            return;
        }

        NSView* buttonContainer = activeButtons[0].superview;
        const CGFloat width = activeButtons[0].frame.size.width;
        const CGFloat height = activeButtons[0].frame.size.height;
        CGFloat spacing = width + 6.0;
        if (activeCount > 1) {
            const CGFloat nativeSpacing =
                activeButtons[1].frame.origin.x - activeButtons[0].frame.origin.x;
            if (nativeSpacing > width) {
                spacing = nativeSpacing;
            }
        }

        if (const std::optional<QPoint> requestedOrigin = controller_.trafficLightOrigin()) {
            const QRect firstButtonRect(*requestedOrigin, QSize(qCeil(width), qCeil(height)));
            const NSRect firstButtonInView = viewRectFromQtRect(firstButtonRect);
            const NSRect firstButtonInContainer = [buttonContainer convertRect:firstButtonInView
                                                                      fromView:view_];
            const CGFloat groupWidth = width + spacing * static_cast<CGFloat>(activeCount - 1);
            const CGFloat minimumX = NSMinX(buttonContainer.bounds);
            const CGFloat maximumX = NSMaxX(buttonContainer.bounds) - groupWidth;
            const CGFloat minimumY = NSMinY(buttonContainer.bounds);
            const CGFloat maximumY = NSMaxY(buttonContainer.bounds) - height;
            const CGFloat x = maximumX >= minimumX
                                  ? std::clamp(NSMinX(firstButtonInContainer), minimumX, maximumX)
                                  : minimumX;
            const CGFloat y = maximumY >= minimumY
                                  ? std::clamp(NSMinY(firstButtonInContainer), minimumY, maximumY)
                                  : minimumY;
            for (std::size_t index = 0; index < activeCount; ++index) {
                [activeButtons[index]
                    setFrameOrigin:NSMakePoint(x + spacing * static_cast<CGFloat>(index), y)];
            }
        }
    }

    void applySystemButtonState() override {
        if (updatingButtonVisibility_) {
            return;
        }
        updatingButtonVisibility_ = true;
        const bool visible =
            controller_.systemButtonsVisible() && !hideButtonsDuringFullScreenExit_;
        const auto buttons = systemButtons();
        for (std::size_t index = 0; index < buttons.size(); ++index) {
            if (buttons[index] != nil) {
                buttons[index].hidden = !visible || !enabledButtons_[index];
                buttons[index].enabled =
                    enabledButtons_[index] && (index != 2 || !controller_.hostSizeFixed());
            }
        }
        updatingButtonVisibility_ = false;
    }

    void updateResizablePolicy() override {
        applyWindowStyle();
        updateButtonObservers();
        applySystemButtonState();
    }

    void prepareSystemMove() override {
        releasePendingMouseDown();
        NSEvent* event = NSApp.currentEvent;
        if (event.type == NSEventTypeLeftMouseDown) {
            pendingMouseDown_ = [event retain];
        }
    }

    void startSystemMove(const QPoint&) override {
        if (window_ != nil && pendingMouseDown_ != nil) {
            NSEvent* event = pendingMouseDown_;
            pendingMouseDown_ = nil;
            [window_ performWindowDragWithEvent:event];
            [event release];
            return;
        }
        if (controller_.window() != nullptr) {
            static_cast<void>(controller_.window()->startSystemMove());
        }
    }

    void cancelSystemMove() override {
        releasePendingMouseDown();
    }

    void handleTitleBarDoubleClick(const QPoint&) override {
        if (window_ == nil) {
            return;
        }
        NSString* action =
            [NSUserDefaults.standardUserDefaults stringForKey:@"AppleActionOnDoubleClick"];
        if ([action caseInsensitiveCompare:@"Minimize"] == NSOrderedSame) {
            [window_ performMiniaturize:nil];
        } else if ([action caseInsensitiveCompare:@"None"] != NSOrderedSame) {
            [window_ performZoom:nil];
        }
    }

    void showSystemMenu(const QPoint&) override {
        // AppKit does not expose the Windows-style title-bar system menu.
    }

    bool centralize() override {
        if (window_ == nil) {
            return false;
        }
        [window_ center];
        return true;
    }

    void raiseWindow() override {
        if (window_ == nil) {
            return;
        }
        if (window_.miniaturized) {
            [window_ deminiaturize:nil];
        }
        [window_ makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }

    void handleNativeWindowChange() {
        synchronize();
        if (settlingFullScreenExit_) {
            scheduleFullScreenExitSettlement();
        }
    }

    void handleNativeButtonVisibilityChange() {
        const bool hasDisabledButton = std::any_of(enabledButtons_.cbegin(), enabledButtons_.cend(),
                                                   [](const bool enabled) { return !enabled; });
        if (hideButtonsDuringFullScreenExit_ || hasDisabledButton ||
            !controller_.systemButtonsVisible()) {
            applySystemButtonState();
        }
    }

    void beginFullScreenTransition(const bool exiting) {
        fullScreenTransition_ =
            exiting ? FullScreenTransition::Exiting : FullScreenTransition::Entering;
        if (exiting) {
            // AppKit temporarily restores its default traffic-light positions
            // during exit. Keep those intermediate positions off-screen, then
            // reveal the buttons after the windowed layout has been restored.
            window_.titleVisibility = NSWindowTitleHidden;
            hideButtonsDuringFullScreenExit_ = hasCustomSystemButtonLayout();
            if (hideButtonsDuringFullScreenExit_) {
                applySystemButtonState();
            }
        }
    }

    void finishFullScreenTransition(const bool exited) {
        fullScreenTransition_ = FullScreenTransition::None;
        if (!exited || !hideButtonsDuringFullScreenExit_) {
            hideButtonsDuringFullScreenExit_ = false;
            settlingFullScreenExit_ = false;
            applyWindowStyle();
            synchronize();
            updateButtonObservers();
            return;
        }

        // NSWindowDidExitFullScreenNotification is emitted before AppKit has
        // necessarily completed the final title-bar layout for this run-loop
        // turn. Keep the buttons hidden until a deferred stable layout pass.
        settlingFullScreenExit_ = true;
        hasExitLayoutFingerprint_ = false;
        stableExitLayoutPasses_ = 0;
        applyWindowStyle();
        synchronize();
        scheduleFullScreenExitSettlement();
    }

  private:
    enum class FullScreenTransition {
        None,
        Entering,
        Exiting,
    };

    struct ButtonLayoutFingerprint final {
        std::array<std::uintptr_t, kNativeButtonCount> buttons{};
        std::array<std::uintptr_t, kNativeButtonCount> containers{};
        std::array<NSRect, kNativeButtonCount> frames{};
        std::array<NSRect, kNativeButtonCount> containerFrames{};
        bool valid = false;
    };

    std::array<NSButton*, kNativeButtonCount> systemButtons() const {
        if (window_ == nil) {
            return {nil, nil, nil};
        }
        return {[window_ standardWindowButton:NSWindowCloseButton],
                [window_ standardWindowButton:NSWindowMiniaturizeButton],
                [window_ standardWindowButton:NSWindowZoomButton]};
    }

    bool hasCustomSystemButtonLayout() const {
        return controller_.trafficLightOrigin().has_value();
    }

    void updateButtonObservers() {
        if (buttonObserver_ == nil) {
            return;
        }
        NSMutableArray<NSButton*>* buttons = [NSMutableArray arrayWithCapacity:kNativeButtonCount];
        for (NSButton* button : systemButtons()) {
            if (button != nil) {
                [buttons addObject:button];
            }
        }
        [buttonObserver_ attachToButtons:buttons];
    }

    void scheduleFullScreenExitSettlement() {
        if (!settlingFullScreenExit_ || lifetimeToken_ == nullptr) {
            return;
        }
        const std::uint64_t generation = ++exitSettlementGeneration_;
        const auto lifetimeToken = lifetimeToken_;
        MacWindowBackend* backend = this;
        dispatch_async(dispatch_get_main_queue(), ^{
          if (!lifetimeToken->load(std::memory_order_acquire)) {
              return;
          }
          backend->completeFullScreenExitSettlement(generation);
        });
    }

    void completeFullScreenExitSettlement(const std::uint64_t generation) {
        if (!settlingFullScreenExit_ || generation != exitSettlementGeneration_ || window_ == nil) {
            return;
        }

        if (usesNativeFullScreenButtonLayout()) {
            hasExitLayoutFingerprint_ = false;
            stableExitLayoutPasses_ = 0;
            scheduleFullScreenExitSettlement();
            return;
        }

        updateButtonObservers();
        applySystemButtonState();
        applyWindowStyle();
        [window_ layoutIfNeeded];
        // AppKit may replace the standard buttons while laying out the final
        // windowed title bar. Observe and hide the current instances again.
        updateButtonObservers();
        applySystemButtonState();
        updateSystemButtonLayout();
        if (generation != exitSettlementGeneration_) {
            return;
        }

        const ButtonLayoutFingerprint fingerprint = captureButtonLayoutFingerprint();
        if (!fingerprint.valid) {
            hasExitLayoutFingerprint_ = false;
            stableExitLayoutPasses_ = 0;
            scheduleFullScreenExitSettlement();
            return;
        }
        if (hasExitLayoutFingerprint_ && buttonLayoutsMatch(exitLayoutFingerprint_, fingerprint)) {
            ++stableExitLayoutPasses_;
        } else {
            exitLayoutFingerprint_ = fingerprint;
            hasExitLayoutFingerprint_ = true;
            stableExitLayoutPasses_ = 1;
        }
        if (stableExitLayoutPasses_ < 2) {
            scheduleFullScreenExitSettlement();
            return;
        }

        settlingFullScreenExit_ = false;
        hideButtonsDuringFullScreenExit_ = false;
        // Unhiding a standard window button is itself layout-affecting on
        // recent AppKit versions: AppKit restores the default (9, 9) origin.
        // Make the custom frame the final write in the same non-animated
        // transaction so no default-position frame can be composited.
        [NSAnimationContext beginGrouping];
        NSAnimationContext.currentContext.duration = 0.0;
        NSAnimationContext.currentContext.allowsImplicitAnimation = NO;
        applySystemButtonState();
        updateSystemButtonLayout();
        [NSAnimationContext endGrouping];
        updateButtonObservers();
    }

    ButtonLayoutFingerprint captureButtonLayoutFingerprint() const {
        ButtonLayoutFingerprint fingerprint;
        const auto buttons = systemButtons();
        for (std::size_t index = 0; index < buttons.size(); ++index) {
            NSButton* button = buttons[index];
            if (button == nil || button.superview == nil || !enabledButtons_[index]) {
                continue;
            }
            fingerprint.buttons[index] = reinterpret_cast<std::uintptr_t>(button);
            fingerprint.containers[index] = reinterpret_cast<std::uintptr_t>(button.superview);
            fingerprint.frames[index] = button.frame;
            fingerprint.containerFrames[index] = button.superview.frame;
            fingerprint.valid = true;
        }
        return fingerprint;
    }

    static bool rectsMatch(const NSRect first, const NSRect second) {
        constexpr CGFloat tolerance = 0.5;
        return std::abs(NSMinX(first) - NSMinX(second)) <= tolerance &&
               std::abs(NSMinY(first) - NSMinY(second)) <= tolerance &&
               std::abs(NSWidth(first) - NSWidth(second)) <= tolerance &&
               std::abs(NSHeight(first) - NSHeight(second)) <= tolerance;
    }

    static bool buttonLayoutsMatch(const ButtonLayoutFingerprint& first,
                                   const ButtonLayoutFingerprint& second) {
        if (!first.valid || !second.valid || first.buttons != second.buttons ||
            first.containers != second.containers) {
            return false;
        }
        for (std::size_t index = 0; index < kNativeButtonCount; ++index) {
            if (!rectsMatch(first.frames[index], second.frames[index]) ||
                !rectsMatch(first.containerFrames[index], second.containerFrames[index])) {
                return false;
            }
        }
        return true;
    }

    bool usesNativeFullScreenButtonLayout() const {
        return fullScreenTransition_ != FullScreenTransition::None ||
               (window_ != nil && (window_.styleMask & NSWindowStyleMaskFullScreen) != 0);
    }

    NSRect viewRectFromQtRect(const QRect& rect) const {
        if (view_ == nil) {
            return NSZeroRect;
        }
        const CGFloat y =
            view_.flipped ? rect.y() : view_.frame.size.height - rect.y() - rect.height();
        return NSMakeRect(rect.x(), y, rect.width(), rect.height());
    }

    void applyWindowStyle() {
        if (window_ == nil || fullScreenTransition_ != FullScreenTransition::None) {
            return;
        }
        NSWindowStyleMask mask = window_.styleMask;
        mask |= NSWindowStyleMaskTitled | NSWindowStyleMaskFullSizeContentView;
        const VSystemButtons configuredButtons = controller_.systemButtons();
        if (configuredButtons.testFlag(VSystemButton::Close)) {
            mask |= NSWindowStyleMaskClosable;
        } else {
            mask &= ~NSWindowStyleMaskClosable;
        }
        if (configuredButtons.testFlag(VSystemButton::Minimize)) {
            mask |= NSWindowStyleMaskMiniaturizable;
        } else {
            mask &= ~NSWindowStyleMaskMiniaturizable;
        }
        if (!controller_.hostSizeFixed()) {
            mask |= NSWindowStyleMaskResizable;
        } else {
            mask &= ~NSWindowStyleMaskResizable;
        }
        if (window_.styleMask != mask) {
            window_.styleMask = mask;
        }
        if (!window_.titlebarAppearsTransparent) {
            window_.titlebarAppearsTransparent = YES;
        }
        const NSWindowTitleVisibility titleVisibility =
            (mask & NSWindowStyleMaskFullScreen) != 0 ? NSWindowTitleVisible : NSWindowTitleHidden;
        if (window_.titleVisibility != titleVisibility) {
            window_.titleVisibility = titleVisibility;
        }
        if (!window_.hasShadow) {
            window_.hasShadow = YES;
        }
        if (!window_.movable) {
            window_.movable = YES;
        }
        if (window_.movableByWindowBackground) {
            window_.movableByWindowBackground = NO;
        }
    }

    void releasePendingMouseDown() {
        if (pendingMouseDown_ != nil) {
            [pendingMouseDown_ release];
            pendingMouseDown_ = nil;
        }
    }

    NSView* view_ = nil;
    NSWindow* window_ = nil;
    VKUINativeWindowObserver* observer_ = nil;
    VKUINativeButtonObserver* buttonObserver_ = nil;
    NSEvent* pendingMouseDown_ = nil;
    std::array<bool, kNativeButtonCount> enabledButtons_{true, true, true};
    bool synchronizing_ = false;
    bool updatingButtonVisibility_ = false;
    FullScreenTransition fullScreenTransition_ = FullScreenTransition::None;
    bool hideButtonsDuringFullScreenExit_ = false;
    bool settlingFullScreenExit_ = false;
    std::uint64_t exitSettlementGeneration_ = 0;
    std::shared_ptr<std::atomic_bool> lifetimeToken_;
    ButtonLayoutFingerprint exitLayoutFingerprint_;
    bool hasExitLayoutFingerprint_ = false;
    int stableExitLayoutPasses_ = 0;
};

std::unique_ptr<VPlatformWindowBackend>
createPlatformWindowBackend(VNativeWindowController& controller) {
    return std::make_unique<MacWindowBackend>(controller);
}

} // namespace vkui::windowing

@implementation VKUINativeWindowObserver

- (instancetype)initWithBackend:(vkui::windowing::MacWindowBackend*)backend {
    self = [super init];
    if (self != nil) {
        backend_ = backend;
    }
    return self;
}

- (void)windowGeometryChanged:(NSNotification*)notification {
    Q_UNUSED(notification)
    if (backend_ != nullptr) {
        backend_->handleNativeWindowChange();
    }
}

- (void)windowFullScreenChanged:(NSNotification*)notification {
    if (backend_ != nullptr) {
        const bool exited =
            [notification.name isEqualToString:NSWindowDidExitFullScreenNotification];
        backend_->finishFullScreenTransition(exited);
    }
}

- (void)windowFullScreenWillChange:(NSNotification*)notification {
    if (backend_ != nullptr) {
        const bool exiting =
            [notification.name isEqualToString:NSWindowWillExitFullScreenNotification];
        backend_->beginFullScreenTransition(exiting);
    }
}

@end

@implementation VKUINativeButtonObserver

- (instancetype)initWithBackend:(vkui::windowing::MacWindowBackend*)backend {
    self = [super init];
    if (self != nil) {
        backend_ = backend;
        buttons_ = [[NSMutableArray alloc] initWithCapacity:3];
    }
    return self;
}

- (void)dealloc {
    [self detach];
    [buttons_ release];
    [super dealloc];
}

- (void)attachToButtons:(NSArray<NSButton*>*)buttons {
    [self detach];
    for (NSButton* button in buttons) {
        [button addObserver:self forKeyPath:@"hidden" options:0 context:nil];
        [buttons_ addObject:button];
    }
}

- (void)detach {
    for (NSButton* button in buttons_) {
        [button removeObserver:self forKeyPath:@"hidden"];
    }
    [buttons_ removeAllObjects];
}

- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id>*)change
                       context:(void*)context {
    Q_UNUSED(keyPath)
    Q_UNUSED(object)
    Q_UNUSED(change)
    Q_UNUSED(context)
    if (backend_ != nullptr) {
        backend_->handleNativeButtonVisibilityChange();
    }
}

@end
