# 0016: Reconcile output changes by semantic value

* Status: Accepted
* Date: 2026-08-01
* Implementation status: Implemented; application-level audio stream recovery
  remains deferred
* Amends:
  [0003: Normalize rendered video to one display-targeted linear surface](0003-display-targeted-video-surface.md)
  and
  [0011: Route selected media once and separate decoded audio from device output](0011-single-pass-media-routing-and-audio-output-boundary.md)

## Context

Earlier plans proposed several display-selection, capability, provider,
topology, and asynchronous-query revisions, while the audio path deliberately
prevented cubeb from migrating the default endpoint so Sunroom could eventually
model every physical clock replacement explicitly.

The implemented system is smaller than those plans. It already has one
semantic presentation target, one compact audio media/hold history, independent
user play intent, and strict identities for media and native-resource
lifetimes. Review of mature players, Windows display contracts, and the pinned
cubeb WASAPI source shows that the additional planned identities do not make
imperfect platform capability data more precise. The fail-closed audio policy
also duplicates migration already performed by cubeb and currently turns an
ordinary device change into a terminal playback error.

Sunroom needs persistent correctness and safe lifetimes. It does not need every
transient display fact or acoustic sample around a route change to form one
globally atomic transaction.

## Decision

Sunroom keeps strict process identities only for:

* `playbackGeneration`, which rejects stale media work.
* `graphicsDeviceGeneration`, which rejects resources from a destroyed GPU
  domain.
* `audioOutputEpoch`, which identifies one cubeb stream lifetime and its
  output-frame-to-media mapping.

A protocol-supplied asynchronous identity, such as a Wayland preferred image
description, remains local to that platform object's lifetime. It is not
promoted into a cross-platform revision hierarchy.

### Display changes

Platform display events are hints to obtain the latest state. The adapter
normalizes that state into one semantic `PresentationTarget`; equality of the
semantic value decides whether target-dependent video must be rerendered.
Native display identity and raw reported capabilities are diagnostic or local
lookup data, not media or cache identities.

Rendered-video reuse records every target value that can affect rendering. It
does not carry a separate display-target revision that can change while those
values remain equal. When future gamut or renderer policy affects the result,
that semantic value is added to the render request rather than hidden behind a
new display generation.

A native output identity change alone does not recreate the swapchain.
Presentation resources are recreated or reconfigured only when their format,
declared color encoding, HDR/SDR mode, adapter/device, size contract, or native
lifetime requires it. A reference-white, peak, minimum-luminance, or gamut
change normally rerenders the retained frame against the new target and then
recomposes it.

Windows uses the cached HWND-bound `DisplayInformation` as the Advanced Color
authority. macOS will query the current `NSScreen` after its normal screen and
profile notifications. Qt 6.11.1's Cocoa backing-property propagation replaces
the existing `CAMetalLayer` color space during a screen transition, so macOS
marks that native surface configuration dirty and re-runs QRhi
`createOrResize()` at the next render boundary even when the swapchain format
is unchanged. This restores the declared encoding without treating display
identity as a video-cache revision or rebuilding the graphics device. Wayland
will use the compositor's preferred surface description and its protocol
identity. Sunroom does not introduce a shared display-topology transaction
model or custom spanning-window authority.

### Audio changes

Cubeb and the platform sound service own normal migration of a stream following
the system default device. Sunroom opens the default output rather than
enumerating and pinning the current endpoint, does not disable cubeb device
switching, and does not carry the fail-closed WASAPI reconfiguration patch.

An `audioOutputEpoch` identifies one cubeb stream, not every native endpoint or
client that cubeb may use internally. Sunroom continues to translate cubeb's
logical presented position through its compact media-versus-hold history. A
backend-managed route change may cause a bounded audible gap or skip; V1 does
not claim gapless or sample-perfect migration.

If cubeb reports an error, stream creation fails, or real-device experiments
demonstrate a persistent no-progress condition, Sunroom may perform one
controlled application-level recreation. That operation freezes the last
confident media time, creates a new output epoch, prerolls, reanchors the new
position, and resumes only if current user intent still requests playback. It
does not normally reopen or reread the media source.

User intent remains a separate boolean. The existing `Buffering` interruption
remains a focused state until more than one concurrent timeline blocker creates
a demonstrated need for a general blocking-reasons set.

## Consequences

Benefits:

* Display observation converges to the latest target without false precision
  or a hierarchy of revisions.
* Equal target values reuse video surfaces and avoid identity-driven redraw or
  swapchain churn.
* Cubeb can perform the cross-platform default-route migration it was selected
  to provide.
* Media generation, GPU lifetime, audio clock meaning, and user intent remain
  explicit.
* Device recovery stays local to audio output and does not create a second
  media reader.

Costs and limitations:

* A few frames may use the old display target while an event is being
  reconciled.
* Backend-managed audio migration can skip audio already queued to an endpoint
  that disappears. The logical clock follows the surviving stream rather than
  claiming exact acoustic continuity.
* Cubeb WASAPI has no successful stream-reconfiguration callback. Sunroom will
  not manufacture a per-migration epoch from unrelated collection events.
* A no-progress watchdog is deferred until real devices demonstrate the need
  and establish a safe threshold.
* Application-level cubeb stream recreation after a backend error remains a
  separate recovery slice.

## Alternatives considered

### Model every display fact as a revisioned snapshot

Rejected. It makes transient observations look atomic without improving the
underlying platform data. Semantic comparison plus local protocol identities
is sufficient.

### Recreate the swapchain on every screen identity change

Rejected as a general rule. Recreate only when the presentation contract or
resource lifetime changes.

### Fail every cubeb reconfiguration and replace the stream ourselves

Superseded for normal default-device following. It gives a precise epoch
boundary but duplicates backend policy and currently turns a recoverable route
change into a fatal session error.

### Infer a new audio epoch from every collection notification

Rejected. The pinned WASAPI collection callback is not stream-specific and is
not a successful-reconfiguration boundary.

### Replace playback state with a general blocking-reasons framework now

Rejected. User intent is already independent and only one interruption state
exists. Add a broader representation only when concrete simultaneous blockers
require it.
