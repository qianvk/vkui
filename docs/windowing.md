# Windowing

`VkUI::Window` is the optional native-window module for macOS and Windows. It
owns full-content setup, system move and resize behavior, native system-button
policy, and multi-title-bar hit testing. Linux is intentionally outside the
current native-window scope; configure with `VKUI_BUILD_WINDOW=OFF` there.

The active implementation depends only on public Qt APIs. The retained
`src/window/qwindowkit` tree is not compiled or linked.

## Architecture

```text
ApplicationWindow : any top-level QWidget
└── owns VWindowAgent               public composition API
    └── PIMPL
        └── VNativeWindowController lifecycle, title bars, typed state
            └── platform backend
                ├── macOS           one existing NSView / NSWindow
                └── Windows         one HWND subclass
```

`VNativeWindowController` creates and observes the top-level native handle,
rebinds the backend if Qt recreates it, owns title-bar and window-wide
client-input regions, and contains no AppKit or Win32 types. Each backend attaches to one window only;
there is no application-global native event filter or Objective-C runtime
method replacement. Windows are resizable and system buttons are always
visible by default; applications may explicitly show or hide them.
`VWindowAgent` receives its host by reference during construction and cannot be
rebound. The application composes it as the window's final data member, so C++
destruction order releases the private controller before the QWidget base
destroys its native handle. Detaching only removes
observers and native subclasses; it never mutates a handle
that Qt may already be destroying. A partial native-style rollback would be
inconsistent because Qt's expanded-client-area flags remain part of the host.

On Qt 6.9 and newer the controller uses `ExpandedClientAreaHint` and
`NoTitleBarBackgroundHint`. Qt 6.6–6.8 use a frameless Qt client model while
the backend restores the native titled or thick-frame window.

## Public boundary

Applications may use any top-level QWidget and compose `vkui::VWindowAgent`:

```cpp
class ApplicationWindow final : public QWidget {
  public:
    ApplicationWindow() : QWidget(nullptr, Qt::Window), windowAgent_(*this) {
        buildContent();
        windowAgent_.setResizable(true);
        windowAgent_.addTitleBar(navigationTitleBar_);
        windowAgent_.addTitleBar(contentTitleBar_);
        windowAgent_.setHitTestVisible(searchField_);
        windowAgent_.setHitTestVisible(splitter_->handle(1));
    }

  private:
    // Keep the agent last so it detaches before QWidget base destruction.
    vkui::VWindowAgent windowAgent_;
};
```

Title bars retain their semantic name because their empty areas support native
window dragging, the platform title-bar double-click action, and the system menu
where one exists. Hit-test-visible widgets are window-scoped: a sibling such as
a full-height splitter handle is registered once and keeps normal client input
wherever it overlaps any title bar. There is no singular `titleBar()` accessor
because a window may have multiple title-bar regions.
The API also does not register replacement `QWidget` system buttons: AppKit and
DWM own the real buttons. The controller configures them during agent
construction and after every native-handle replacement. Native controls are an
invariant of the supported backends rather than a runtime capability. Visibility
is controlled only by `setSystemButtonsVisible(bool)`; the independently typed
`setSystemButtons(VSystemButtons)` selects the configured native set:

```cpp
windowAgent_.setSystemButtons(vkui::VStandardSystemButtons);
windowAgent_.setSystemButtons(vkui::VSystemButton::Close);
windowAgent_.setSystemButtonsVisible(false); // Temporary visibility policy.
```

Visible buttons retain their platform-native hit testing, behavior, and
accessibility. On Windows, DWM always owns caption-button layout; combinations
that Windows cannot render independently retain the closest native behavior.
The cross-platform Standard, Close-only, and hidden configurations are exact.
Platform-specific string attributes are intentionally not part of the public contract.

### Gallery transparent title bars

The Gallery keeps each logical title-bar hit region behind its content surface.
Title-bar controls are direct overlay children positioned by a layout that paints
no background. This is real transparency: an empty title-bar area reveals the
page below instead of propagating an opaque parent color through a transparent
sibling widget.

Every Gallery page is hosted by one shared scroll-container implementation. Its
56-pixel initial top inset belongs to the scrollable canvas rather than to
`QAbstractScrollArea::viewportMargins()`. At scroll position zero, the inset
places page content below the title-bar controls. As the scrollbar advances,
the inset and page move together beneath the fixed controls; content is clipped
only by the full-height viewport at the window's top edge. A viewport margin
would be incorrect here because it permanently shrinks the viewport and forces
content to disappear at the title bar's lower edge.

## Why PIMPL is used

PIMPL means *pointer to implementation*. The installed header declares only an
incomplete private type and stores one owning pointer:

```cpp
class VWindowAgentPrivate;

class VWindowAgent final : public QObject {
    // Public API only.
    std::unique_ptr<VWindowAgentPrivate> d_;
};
```

The implementation file defines the hidden state:

```cpp
class VWindowAgentPrivate final {
  public:
    explicit VWindowAgentPrivate(QWidget& host) : controller(host) {}
    VNativeWindowController controller;
};
```

This keeps controller and platform headers out of application translation
units, prevents AppKit/Win32 implementation details from entering the public
ABI, and reduces downstream rebuilds when internals change. Its cost is one
allocation per window and one forwarding indirection; native event dispatch and
painting costs dominate that constant setup cost.

## Ownership and native-handle lifetime

The application window owns both Qt content and its composed native behavior:

```text
ApplicationWindow
├── QWidget base (owns the platform window)
└── VWindowAgent final member
    └── private implementation
        └── VNativeWindowController value
            └── platform backend (temporarily attached to WId)
```

The host is injected once through `VWindowAgent(QWidget&)`; there is no later
`setup()` or rebinding operation. The agent intentionally has no QObject parent,
because a QObject parent must never delete a C++ data member. Declaring the
agent last makes it the first member destroyed, while the QWidget base and its
native handle remain valid. The controller still uses `QPointer` for both
`QWidget` and `QWindow`, because Qt may recreate the platform window while the
host continues to exist.

Calling `QWidget::winId()` creates a native handle if necessary. Qt sends
`QEvent::WinIdChange` whenever that handle changes. The host event filter handles
that event by removing the old `QWindow` event filter, detaching the backend,
reading the new `QWindow` and `WId`, attaching the backend to the replacement,
and replaying the typed controller state. `QEvent::Show` runs the same idempotent
refresh because some platform plugins finish native-window creation at show
time.

`detach()` means *release this borrowed native attachment*, not *destroy or
restore the window*. On macOS it invalidates deferred callbacks, releases a
pending mouse event, removes notifications and button
observers, then clears the borrowed `NSView*` and `NSWindow*`. On Windows it
removes the per-`HWND` subclass and clears the borrowed handle. Neither backend
owns the native window, and neither writes style or frame state while the old
handle may be in destruction.

## macOS backend

Qt Cocoa creates the `NSView` and `NSWindow`. VkUI obtains that existing
window, retains `NSWindowStyleMaskTitled`, enables full-size content, and lets
AppKit own:

- traffic-light rendering, actions, accessibility, and Retina behavior;
- native shadow and outer window corners;
- live resize, full screen, Spaces, minimize, and zoom transitions;
- window dragging through `performWindowDragWithEvent:`.

VkUI owns only custom-title-bar region mapping, optional traffic-light
placement, and explicit show/hide state. Custom placement is expressed as one top-left group
origin in Qt logical window coordinates:

```cpp
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
windowAgent_.setTrafficLightOrigin(QPoint(15, 21));
#endif
```

AppKit continues to own the three buttons' dimensions, native spacing,
rendering, actions, and accessibility. VkUI clamps the complete group inside
the current AppKit button container; it never assigns independent positions to
individual buttons. During full-screen transitions AppKit exclusively
owns traffic-light placement. AppKit temporarily applies default button
coordinates while exiting full screen, so VkUI hides the buttons at
`NSWindowWillExitFullScreenNotification`, observes `NSButton.hidden` to reject
AppKit's intermediate re-show, and waits for the final windowed button container
to remain structurally stable across deferred layout passes. Revealing a standard
button can itself reset its frame, so VkUI performs the reveal first and makes
the custom frame the final write in a zero-duration `NSAnimationContext`.
Geometry synchronization never reapplies `NSWindow.styleMask`; native window
properties are changed only when their value actually differs. Notifications
are scoped to the attached `NSWindow`.

`VWindowAgent::centralize()` delegates to AppKit's native `[NSWindow center]`
when attached to Cocoa. AppKit intentionally places the window horizontally
centered and slightly above the geometric vertical center. The portable Qt
calculation remains only as a fallback for backends without a native centering
operation.

## Windows backend

The Windows backend installs a per-window `SetWindowSubclass` handler. It
retains the native caption/thick-frame styles, delegates caption-button hit
testing to `DwmDefWindowProc`, preserves system resize borders, and uses DWM
caption controls so Windows 11 Snap Layout and native accessibility remain
available. Custom title-bar hit testing returns `HTCAPTION`; resize edges are
resolved in physical window coordinates with the current DPI, then converted
to Qt logical coordinates for widget hit testing.
Windows exposes no caption-button positioning operation: DWM exclusively owns
their bounds and layout.

The backend never installs an application-global `QAbstractNativeEventFilter`.

## Dialog primitives

`VMessageDialog` is a self-contained `QDialog` with private full-content
chrome, no system buttons, severity icons, platform-ordered actions, explicit
default and Escape actions, and selected-text-safe message rendering. It
composes `VWindowAgent` internally; the title bar, layout, and button box are
implementation details rather than public extension points.
`confirmDestructive()` always makes Cancel both the default button and the
Escape action, so pressing Enter cannot accidentally confirm a destructive
operation.

The public button contract follows Qt's established message-box model. Callers
may add standard or custom buttons, inspect or change semantic roles, remove a
button without deleting it, or clear every button. The internal
`QDialogButtonBox` remains private so layout and platform ordering can evolve
without breaking applications.

```cpp
vkui::VMessageDialog prompt(vkui::VMessageDialog::Icon::Warning,
                            tr("Replace file?"), tr("A file already exists."),
                            QDialogButtonBox::Cancel);
auto* replace = prompt.addButton(tr("Replace"), QDialogButtonBox::DestructiveRole);
prompt.setDefaultButton(QDialogButtonBox::Cancel);
prompt.setEscapeButton(QDialogButtonBox::Cancel);
connect(&prompt, &vkui::VMessageDialog::buttonClicked, &prompt,
        [replace](QAbstractButton* clicked) {
            if (clicked == replace) {
                // Apply the application-owned operation.
            }
        });
prompt.exec();
```

### Preferences window structure

Preferences are an application-level window, not a dialog primitive. The
gallery implements its concrete settings UI as a top-level `QWidget` and
composes `VWindowAgent` directly:

```text
GalleryApplicationController
├── GalleryWindow
└── QPointer<GalleryPreferencesWindow>  zero or one live instance
    └── GalleryPreferencesWindow : QWidget
        ├── settings content
        └── VWindowAgent                close-only, full-content chrome
```

The application controller creates Preferences on first use. While it exists,
all requests activate that same window. `WA_DeleteOnClose` destroys the window
after an accepted native close event, and `QPointer` becomes null
automatically. A later request creates a fresh instance, so an unused
Preferences window retains no memory. The controller explicitly deletes any
still-live instance during application shutdown while `QApplication` and the
native platform integration remain available.

```cpp
class GalleryPreferencesWindow final : public QWidget {
  public:
    GalleryPreferencesWindow()
        : QWidget(nullptr, Qt::Window), windowAgent_(*this) {
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_QuitOnClose, false);
        windowAgent_.setSystemButtons(vkui::VSystemButton::Close);
        windowAgent_.setResizable(true);
        windowAgent_.addTitleBar(titleBar_);
    }

  private:
    vkui::VWindowAgent windowAgent_;
};
```

This is an application-scoped single-live-instance policy, not a global static
Singleton. It preserves main-thread ownership, testability, and deterministic
destruction ordering. Settings state lives in the application model or
`VkThemeManager`, not in the disposable window instance.

The close button remains the platform-native control. AppKit or Windows sends
the native close request, Qt translates it to `QCloseEvent`, and the widget's
`closeEvent()` decides whether to accept or ignore it. Applications may
therefore validate, save, hide, or veto a close without replacing or retargeting
the native button. Gallery accepts it and relies on `WA_DeleteOnClose`.

### Confirm window structure

`VMessageDialog` owns only prompt-specific UI and delegates native window
behavior to a private `VWindowAgent`:

```text
VMessageDialog : QDialog
├── private full-content title bar
├── severity icon + selectable message
├── private platform-ordered QDialogButtonBox
└── VWindowAgent (system-button set is empty)
```

For destructive confirmation, use the policy-bearing helper instead of
reimplementing default-button behavior:

```cpp
const bool confirmed = vkui::VMessageDialog::confirmDestructive(
    host, tr("Delete the file?"), tr("This action cannot be undone."), tr("Delete"));
```

The helper runs a window-modal prompt when a host is supplied, hides native
system buttons, and maps both Enter and Escape to Cancel. The destructive
button is activated only by an explicit click or Space while it has focus.

## Preferences guidance

VkUI intentionally does not own application settings models. A Preferences
window should:

1. be implemented in the application as a top-level `QWidget`;
2. compose `VWindowAgent` and request the close-only native button set;
3. keep navigation, pages, and settings bindings in the application;
4. use an application controller to enforce zero or one live instance;
5. decide explicitly whether close hides or destroys the instance;
6. route destructive actions through the generic `VMessageDialog` primitive.
