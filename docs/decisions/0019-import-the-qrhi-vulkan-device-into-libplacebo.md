# 0019: Import the QRhi Vulkan device into libplacebo

* Status: Accepted
* Date: 2026-08-02
* Related:
  [0001: Application-owned QRhi with redirected Qt Quick](0001-application-owned-qrhi-composition.md),
  [0004: Establish one graphics-device domain and explicit video interop seams](0004-cross-platform-graphics-domain-and-video-interop.md)

## Context

The native Wayland port needs Qt Quick, SunPlayer's final compositor, and
libplacebo video rendering to share one Vulkan device. Giving Qt and
libplacebo separate devices would require cross-device image export/import and
would duplicate queue, feature, recovery, and lifetime policy. Creating the
device in libplacebo and importing it into QRhi would also take device creation
away from Qt's normal Vulkan path before a concrete requirement justified it.

Qt 6.10's QRhi Vulkan backend exposes the selected physical device, logical
device, graphics queue, queue family, and queue index. Libplacebo 7.360 can
borrow those objects through `pl_vulkan_import`. QRhi-owned textures expose
their `VkImage` and current layout, and libplacebo can wrap such an image
without taking ownership.

The integration still needs an honest feature contract and a producer-to-
consumer handoff. Vulkan loader version alone does not prove the API version or
features of QRhi's selected physical device, and importing a feature set that
Qt did not enable would be invalid.

## Decision

On Linux:

* `LinuxWaylandWindowContext` owns the `QVulkanInstance` for the native window.
* QRhi creates and owns the physical-device selection, logical device, and
  graphics queue for each recoverable graphics-domain generation.
* SunPlayer queries the selected physical device and requires Vulkan 1.3,
  `hostQueryReset`, timeline semaphores, and synchronization2. It supplies
  libplacebo with the core 1.0 through 1.3 feature chain that Qt 6.10 enables,
  excluding the two robustness features Qt deliberately disables.
* Libplacebo imports and borrows QRhi's instance, physical device, device, and
  one graphics queue. The selected graphics/present queue family must also
  support compute because libplacebo creates its compute command pool from the
  imported family. SunPlayer rejects an incompatible QRhi selection before
  import. Libplacebo never destroys the borrowed native objects.
* One recursive execution mutex serializes CPU access to that queue. The
  recursive form lets a presentation execution scope call libplacebo, whose
  queue callbacks acquire the same guard.
* The libplacebo target is one QRhi-owned RGBA16F image wrapped once with
  `pl_vulkan_wrap`. Software-decoded input may still be uploaded by
  libplacebo, but there is no output-target CPU copy or second full-frame
  target copy.
* Producer access uses libplacebo's Vulkan release/hold API plus one target-
  owned timeline semaphore. Before releasing the image to libplacebo, SunPlayer
  submits the next timeline signal on the shared queue after all earlier QRhi
  work and passes that exact value as libplacebo's wait dependency. This makes
  prior QRhi sampling available before the next producer write without a
  queue-idle stall.
* After rendering, libplacebo holds the image in the deterministic
  `SHADER_READ_ONLY_OPTIMAL` layout and signals the next timeline value. Before
  QRhi samples it, SunPlayer records one same-layout synchronization2 image
  barrier in the current QRhi command buffer, covering prior memory writes and
  making them visible to fragment sampling. `QRhiTexture::setNativeLayout()`
  reconciles Qt's layout tracker with that external handoff.
* Target destruction waits for libplacebo GPU work before destroying the
  timeline semaphore, including producer replacement while the graphics domain
  remains alive.
* Device loss abandons active producer access into the existing canonical
  aborted-submission state. Impossible begin/end call order remains a failed
  programming invariant rather than becoming a device-loss fallback.
* Teardown releases presentation resources and waits for outstanding QRhi work,
  destroys libplacebo's borrowed wrappers, then destroys QRhi and its owned
  device. The window context and Vulkan instance outlive the domain.

VAAPI/DRM PRIME extensions and import objects are not requested speculatively.
That slice will add only the device extensions and import state its measured
descriptor path consumes.

## Consequences

* Linux follows Qt's standard Vulkan device lifecycle instead of maintaining a
  second device creator.
* Qt Quick, the final compositor, and libplacebo share one graphics queue and
  one recovery generation.
* Direct output-target rendering has zero target copies. This does not claim
  zero-copy software decoding or the future VAAPI input path.
* The contract is intentionally tied to the supported Qt 6.10 private QRhi
  implementation and must be revalidated when that family changes.
* Vulkan 1.3 is now the Linux runtime floor. The earlier configure-time 1.2
  loader floor is insufficient for the selected synchronization contract.
* Devices whose only QRhi-compatible graphics/present queue lacks compute are
  unsupported by the direct libplacebo import path.
* Native GPU and VAAPI evidence remains separate from the WSLg llvmpipe proof.

## Alternatives considered

### Let libplacebo create the Vulkan device

Rejected for the current implementation. QRhi ownership works with the
standard Qt lifecycle and satisfies libplacebo's measured requirements. A
second creation direction would add feature, extension, queue, and teardown
policy without solving a present defect.

### Use separate Qt and libplacebo devices

Rejected. Cross-device external-memory and semaphore exchange is more complex
and offers no benefit for the single-window player.

### Copy the libplacebo output into a QRhi texture

Rejected as the normal path. The direct wrapped target is validated and avoids
an unnecessary full-frame output copy. A copy fallback should be added only if
a supported device demonstrates a real incompatibility.

### Wait for the queue or device after every frame

Rejected. The same-queue timeline dependency and the barrier recorded into
QRhi's command buffer provide the required memory dependencies without
serializing CPU and GPU progress on every frame.
