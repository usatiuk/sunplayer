# Windows background before the first QRhi presentation

## Question

Why does SunPlayer briefly show a white client area while opening on Windows,
and how can it provide a black startup background without weakening the D3D11
HDR presentation path?

This investigation covers Windows with the project-pinned Qt 6.11.1 D3D11
backend. macOS and native Wayland do not share this native message path.

## Observed lifecycle

`main.cpp` calls `PresentationWindow::show()` before entering the event loop.
Windows then asks Qt to erase and paint the newly visible HWND. Qt turns
`WM_PAINT` into the exposure that lets SunPlayer create its QRhi swapchain and
render the redirected Qt Quick scene. The first application-owned frame is
black, but it cannot reach DWM until `QRhi::endFrame()` successfully presents
the swapchain.

In the pinned Qt source:

* `QWindowsWindowClassDescription` registers an ordinary `QWindow` class with
  no background brush;
* `QWindowsWindow::handleWmPaint()` acknowledges `WM_ERASEBKGND` without
  painting and uses `WM_PAINT` to deliver exposure; and
* `QWindowsWindow::setOpacity()` implements non-opaque accelerated windows by
  enabling `WS_EX_LAYERED` and calling `SetLayeredWindowAttributes()`.

The dark `QPalette` therefore styles the redirected Quick scene and native
frame but does not initialize the pre-swapchain client pixels. The visible
white interval belongs to the HWND/DWM redirection surface, not QML, video
rendering, or the final compositor, which explicitly clears to black.

The extracted Qt sources under `D:/Qt/6.11.1/Src/qtbase` supplied the exact
implementation evidence. They are an installed dependency snapshot, not a
repository source of truth, and must be rechecked when Qt changes.

## Platform constraints

Microsoft's flip-model documentation says that after the first successful
flip-model presentation, GDI no longer works with the associated HWND. Its
broader guidance also requires a flip-model HWND to have one Direct3D producer
rather than combining GDI and Direct3D content. Flip presentation is required
for unclipped HDR swapchain formats and color spaces.

Primary sources:

* [Qt: `QWindow::opacity`](https://doc.qt.io/qt-6/qwindow.html#opacity-prop)
* [Microsoft: DXGI flip model](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model)
* [Microsoft: `DXGI_SWAP_EFFECT`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect)
* [Microsoft: prefer the flip model](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model)

## Accepted boundary

SunPlayer may fill `WM_ERASEBKGND` with black only while the current native
surface has not completed a successful QRhi presentation. It must let Qt
continue to handle `WM_PAINT`, because that exposure drives first presentation.
Once `QRhi::endFrame()` succeeds, Direct3D owns the client area for the rest of
that native-surface lifetime and SunPlayer performs no further GDI painting.

The state belongs to the presentation engine because only that owner knows
whether presentation succeeded. A newly created native surface starts a new
pre-presentation interval; swapchain resize, output change, or graphics-device
recovery on the same surface does not.

Black matches both the compositor clear and Player's no-frame background. A
failed best-effort native fill falls through to Qt's normal processing without
delaying exposure or changing graphics recovery.

## Alternatives

### Change the application palette

Rejected. The palette does not paint the raw `QWindow` client area before the
swapchain exists.

### Set window opacity to zero until the first frame

Rejected. Qt changes the accelerated HWND to `WS_EX_LAYERED`. That is a larger
native presentation change than the defect requires and creates unnecessary
risk for D3D11 flip-model HDR presentation.

### Cloak or delay showing the window

Rejected. The swapchain needs an exposed native surface, and native cloaking
would add a second visibility lifecycle plus delayed launch/taskbar behavior.

### Paint the client area with GDI permanently

Rejected. It violates the flip-model ownership boundary after first present
and cannot be relied on once DXGI presentation owns the HWND.

## Verification consequence

A Windows-only application probe should send `WM_ERASEBKGND` to the real
`PresentationWindow` with a contrasting memory DC before the first frame and
verify that the pixel becomes black. Existing playback and fullscreen
scenarios must continue to prove that QRhi/D3D presentation proceeds normally.
Repeated visible cold starts in SDR and HDR desktop modes remain the physical
check for absence of a transient flash.

The accepted choice is recorded in
[ADR 0022](../decisions/0022-paint-windows-background-only-before-first-present.md).
