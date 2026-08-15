# Testing tools and boundary research

* Date: 2026-07-28
* Status: Incorporated into `docs/TESTING.md` and
  `docs/subsystems/testing/PLAN.md`

## Question

Which testing tools and boundaries fit SunPlayer now, and which parts of the
long-term whole-pipeline, GPU, and physical-output strategy are supported by
current upstream facilities rather than requiring project-specific
infrastructure?

## Project stage at the start of the investigation

SunPlayer is currently a Windows D3D11/QRhi HDR presentation playground:

* No CTest or test target exists.
* No FFmpeg, libplacebo, libass, playback, or audio pipeline exists.
* The concrete boundaries available to test are display-target policy,
  graphics resource lifecycle, redirected Qt Quick, and final QRhi composition.

This makes a large application-control protocol or media corpus premature, but
it does not justify postponing all tests.

## Findings

### CTest and Qt Test fit the first milestone

CMake's CTest module provides a standard `BUILD_TESTING` option and test
registry. Qt Test already supports data-driven tests, Qt event-loop behavior,
signal observation, GUI event simulation, and benchmarks.

Adopting these first avoids a new general-purpose test dependency and can cover
the current presentation calculations strongly.

Primary sources:

* [CMake CTest module](https://cmake.org/cmake/help/latest/module/CTest.html)
* [Qt Test overview](https://doc.qt.io/qt-6/qtest-overview.html)
* [QSignalSpy](https://doc.qt.io/qt-6/qsignalspy.html)

### Qt Quick Test is narrower than application UI coverage

Qt Quick Test is an appropriate QML unit-test framework. It does not by itself
exercise SunPlayer's redirected `QQuickRenderControl` integration, native
presentation window, custom input forwarding, or packaged application.

It should be selected when isolated player QML components exist, not installed
as proof of end-to-end UI coverage.

Primary source:

* [Qt Quick Test](https://doc.qt.io/qt-6/qtquicktest-index.html)

### Real QRhi capture is possible but has an explicit contract

Qt's redirected-rendering example demonstrates rendering Qt Quick into a
`QRhiTexture`. QRhi can read raw texture data back asynchronously through
`QRhiResourceUpdateBatch::readBackTexture()`.

Constraints relevant to a SunPlayer harness:

* A source texture needs `UsedAsTransferSource`.
* Multisample textures cannot be read back directly.
* The capture returns raw bytes; pixel format, byte order, alpha, orientation,
  and luminance interpretation remain the application's responsibility.
* Floating-point readback must be capability-checked, particularly on OpenGL
  backends.
* QRhi remains a private API with limited compatibility guarantees, so capture
  support should remain behind the existing narrow graphics boundary.

An offscreen real-GPU capture proves QRhi resource, shader, and compositor
behavior. It does not prove a native swapchain, operating-system desktop
compositor, display event adapter, or physical HDR output.

Primary sources:

* [QQuickRenderControl RHI example](https://doc.qt.io/qt-6/qtquick-rendercontrol-rendercontrol-rhi-example.html)
* [QRhi texture readback](https://doc.qt.io/qt-6/qrhiresourceupdatebatch.html)
* [QRhi API and feature reporting](https://doc.qt.io/qt-6/qrhi.html)

### A local control channel is feasible but not yet justified

Qt's `QLocalServer` and `QLocalSocket` provide event-driven local IPC using
named pipes on Windows and local-domain sockets on Unix. They are a reasonable
candidate for a private, opt-in application test channel.

Qt does not provide SunPlayer's command protocol, event schema, scenario
language, or application runner. Those would be project facilities and should
be designed from several real scenarios rather than one illustrative syntax.

Primary sources:

* [QLocalServer](https://doc.qt.io/qt-6/qlocalserver.html)
* [QLocalSocket](https://doc.qt.io/qt-6/qlocalsocket.html)

### OpenEXR and OpenImageIO are credible later candidates

OpenEXR supports 16- and 32-bit floating-point channels and explicit
colorimetric metadata, making it suitable for high-dynamic-range regression
captures.

OpenImageIO already implements common numerical image comparison statistics,
thresholds, and difference output. Its documented comparison API does not
supply every proposed metric, and perceptual comparisons have color-space
assumptions that must not be applied blindly to arbitrary HDR data.

Before adopting either dependency, SunPlayer must define the capture contract:
primaries, transfer, reference white, luminance scale, alpha, orientation, and
NaN/infinity policy. A first analytic QRhi test can compare raw half-float
values without introducing an image-corpus toolchain.

Primary sources:

* [OpenEXR technical introduction](https://openexr.com/en/latest/TechnicalIntroduction.html)
* [OpenImageIO comparison and statistics](https://openimageio.readthedocs.io/en/stable/imagebufalgo.html#image-comparison-and-statistics)

### Dependency suites do not replace integration tests

FFmpeg FATE is FFmpeg's regression suite. libplacebo also ships its own tests.
Running those suites for the pinned dependency builds is useful dependency and
toolchain verification.

Neither suite proves SunPlayer's metadata precedence, `AVFrame` mapping,
native-texture lifetime, synchronization, display target, compositor contract,
or fallback diagnostics. Those need SunPlayer scenarios using the real
dependencies.

Primary sources:

* [FFmpeg FATE](https://ffmpeg.org/fate.html)
* [libplacebo testing](https://github.com/haasn/libplacebo#testing)
* [libplacebo renderer API](https://libplacebo.org/renderer/)
* [libplacebo FFmpeg utilities](https://github.com/haasn/libplacebo/blob/master/src/include/libplacebo/utils/libav.h)

### Graphics validation modes are distinct coverage

Qt can request underlying graphics debug layers through
`QQuickGraphicsConfiguration`. Khronos documents standard Vulkan validation
alongside separately enabled synchronization and GPU-assisted validation.

A future matrix must report these modes separately. A generic "validation
enabled" label must not imply synchronization or GPU-assisted coverage that was
not active.

Primary sources:

* [Qt Quick graphics configuration](https://doc.qt.io/qt-6/qquickgraphicsconfiguration.html)
* [Khronos development and validation tools](https://docs.vulkan.org/guide/latest/development_tools.html)

## Resulting direction

Adopt now:

1. CTest and Qt Test.
2. Pure table-driven presentation-target policy tests.
3. Render-surface device/display-generation tests with graphics milestone 1.
4. A separately selectable real D3D11 QRhi analytic capture test.
5. A recorded manual Windows SDR/HDR display matrix.

Adopt with the corresponding feature:

* Qt Quick Test for isolated player QML.
* Pinned media fixtures with FFmpeg/libplacebo integration.
* Controlled clocks, audio sinks, and source faults with scheduling and I/O.
* A local application-control channel when several process-level scenarios
  need it.
* OpenEXR/OpenImageIO when the floating-point corpus outgrows direct analytic
  comparison.
* Dedicated GPU validation, native display automation, and physical
  measurement as platform support matures.

## Unresolved

* The first concrete factoring of presentation policy for testability.
* The exact supported readback formats on each future QRhi backend.
* Whether raw half-float fixtures remain sufficient or an EXR corpus becomes
  valuable.
* Scenario protocol and storage syntax.
* CI provider, GPU-host topology, and artifact retention.
* Hardware-lab equipment and automation.
