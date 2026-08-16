# Windows MSIX and Microsoft Store packaging research

* Date: 2026-08-16
* Status: Accepted and implementation-validated

## Question

What is the smallest reproducible path from SunPlayer's existing Windows
Release install tree to a Microsoft Store package, while keeping Qt deployment,
package identity, signing, certification, and public-distribution obligations
at their proper boundaries?

This note also records whether the first Store package should run at normal
desktop trust or inside AppContainer.

## Grounded project state

SunPlayer already has the correct package-input boundary:

* CMake installs `sunplayer.exe` under `bin` through `GNUInstallDirs`.
* `x_vcpkg_install_local_dependencies()` stages the transitive non-Qt runtime
  DLL closure.
* `qt_generate_deploy_qml_app_script()` stages Qt libraries, plugins, imported
  QML modules, and the relative `qt.conf`. The script deliberately omits
  translations, QML debugging plugins, Qt's redistributable installer, and
  app-local D3D/DXC compiler copies.
* CMake's `InstallRequiredSystemLibraries` selects the matching app-local Visual
  C++ runtime from the toolset used for the build.
* Windows CI already creates a clean Release install tree, rejects excluded
  DLLs, and runs the installed executable's `--verify-qml` probe before
  publishing a short-lived developer artifact on trusted events.

The packaging layer therefore must consume `cmake --install` output. It must
not copy Qt DLLs itself, invoke `windeployqt` from a post-build command, or
package the ordinary build tree.

Current blockers are outside the mechanics of MSIX creation:

* `project(SunPlayer VERSION 0.1.0)` does not yet express a public Store
  release version. Store package versions use four integer components in
  `0..65535`, reserve the fourth component as zero, and do not allow a zero
  first component.
* The repository explicitly withholds approval for public binaries until the
  third-party notice, corresponding-source/build-recipe, LGPL source-offer,
  FFmpeg/libass codec-policy, and redistribution work is complete.
* A Partner Center product has not supplied the manifest's durable Store
  identity values.

## Packaging-tool findings

### Qt and CMake own the deployed application tree

Qt's QML deployment CMake API is designed to complete an installed QML
application with the Qt libraries, plugins, and QML runtime parts it needs.
SunPlayer already installs its other runtime dependencies before that script
runs. The installed tree is consequently the one input that is meaningful both
before and after MSIX exists.

Qt otherwise deploys `bin/vc_redist.x64.exe`; an installer embedded in an MSIX
payload is inert. SunPlayer therefore uses Qt's `NO_COMPILER_RUNTIME` option and
CMake's toolset-aware `InstallRequiredSystemLibraries` module to install the
app-local runtime. The package adapter consumes that tree unchanged and records
the shipped CRT names, versions, and hashes. This avoids coupling to an installed
`Microsoft.VCLibs` framework package or to whichever developer shell happens to
run packaging.

### `winapp` is the accepted narrow MSIX adapter

Microsoft's Windows App Development CLI supports C++/CMake applications,
manifest and asset generation, loose-layout execution, developer
certificates, MSIX packing, and multi-architecture bundles. Its `package` command
accepts one or more ready-to-package directories and an explicit executable
path relative to each directory. `run` registers a full loose layout and
launches it with package identity, which is a useful intermediate test between
an unpackaged install tree and an installed MSIX.

The tool is still in public preview. Release automation must pin an exact
reviewed release and verify the downloaded archive rather than installing a
moving `latest` version. The official guide's examples that download tools or
generate certificates during CMake configuration are not appropriate for
SunPlayer: ordinary configuration must remain offline from packaging tooling,
and development keys must not become build inputs.

One small PowerShell packaging entry point around the installed tree and the
pinned CLI is sufficient. The 2026-08-16 spike accepted `winapp` 0.6.0 and
`Microsoft.Windows.SDK.BuildTools` 10.0.28000.2526 after proving loose
registration, real fullscreen/playback/audio execution, unsigned packaging,
development signing, installation, stable-identity update, cold launch,
uninstall, exact certificate cleanup, and payload inspection against
SunPlayer's real install tree. The official 0.6.0 x64 installer SHA-256 is
`DC5D323F6D1601EF3342420746F0163651176F4CC183690F0354546A36648EEC`.
Introducing CPack's
External generator, a Visual Studio packaging project, or project-owned
MakeAppx orchestration would add an abstraction without another current package
format consuming it. If `winapp` changes incompatibly before leaving preview,
only that thin adapter should need replacement; the CMake install boundary and
manifest remain stable.

`winapp package` must generate PRI data so Windows can select the committed
scale and target-size asset variants. With pinned `winapp` 0.6.0, the package
contains `resources.pri`, scale resource packages, and the tool's input files
`pri.resfiles` and `priconfig.xml`. The adapter validates that every listed
resource is a relative, existing payload path and records all generated files.
Retaining these harmless generator inputs is preferable to bypassing the
high-level CLI with project-owned MakePri/MakeAppx repacking; revisit the
inventory when the pinned preview tool changes.

### Manifest and assets are reviewed source inputs

`winapp manifest generate` is useful once as a scaffold, not as an
authoritative release-time generator. The reviewed manifest is checked in as a
template. Store mode derives `major.minor.patch.0` from the CMake project version
and reads exact Partner Center identity from checked-in `StoreIdentity.json`.
It rejects an install tree whose CMake-derived Windows ProductVersion differs.
CI cannot override either authority. Development modes retain one
obviously non-production identity and version.

Before the final Store package is produced, reserve the product in Partner
Center and commit these values exactly from Product identity:

* `Package/Identity/Name`
* `Package/Identity/Publisher`
* `Package/Properties/PublisherDisplayName`

Choose and freeze the package-relative application ID (`Application/@Id`) as
`SunPlayer` before first publication because it participates in the AUMID. The
desktop target-device-family entry must include both its minimum version and a
reviewed `MaxVersionTested`. The checked-in value is build 26200, matching the
Windows 11 25H2 host used for the 2026-08-16 package validation; changing it is
a reviewed source change after testing a newer build, not a generator default.
The current English-only application declares one
`Package/Resources/Resource` with `Language="en-US"`; adding translations later
requires synchronizing this list with the languages actually shipped and
declared in Partner Center.

`winapp manifest update-assets` generated the committed scale, target-size,
unplated, and ICO variants from the original project-owned
`packaging/windows/branding/SunPlayer.svg`. Run it with the pinned tool only
when the source branding changes, inspect the output, and commit both the
source and generated assets so a release does not depend on an unreviewed
renderer change. The generated ICO is also the executable's Windows resource.

The first manifest targets only `Windows.Desktop` and x64. ARM64 becomes a
bundle input only after SunPlayer's Windows ARM64 dependency, graphics,
hardware-decode, audio, and CI contracts exist.

## Trust level and capabilities

### Chosen v1 behavior: packaged classic, medium integrity

The first Store package runs as the existing non-elevated desktop application.
A generated `winapp` scaffold may express the entry point as
`$targetentrypoint$`; the reviewed materialized manifest must resolve that to
`Windows.FullTrustApplication` and declare the required `runFullTrust`
restricted capability. Despite that capability's name, the process runs at
normal medium integrity; it is not administrator elevation.

`Windows.FullTrustApplication` is the older-manifest equivalent of
`uap10:RuntimeBehavior="packagedClassicApp"` plus
`uap10:TrustLevel="mediumIL"`. Using the entry point avoids introducing the
`uap10` namespace's Windows 10 build 19041 minimum and preserves SunPlayer's
Windows 10 1809 technical floor. In 2026, an actual 1809 support claim requires
a fully patched Enterprise LTSC 2019 test system; ordinary 1809 editions are
out of servicing. Otherwise the declared product/package floor must rise. The
package must not request elevation, `unvirtualizedResources`, broad filesystem
access, library, network, device, or other capabilities that current behavior
does not require.

MSIX package identity and Windows' packaged-desktop virtualization do not
require a second settings implementation. The existing `QSettings` code stays
unchanged. Package tests must instead verify settings across restart and
upgrade, document the clean-uninstall result, and confirm that no runtime path
writes into the read-only installation directory. SunPlayer has no supported
public unpackaged predecessor, so the first Store release creates no durable
unpackaged-to-packaged migration contract. Revisit that only if another public
Windows distribution ships before the Store package.

### AppContainer is a separate future security experiment

AppContainer is attractive for a process parsing untrusted media, and Windows
supports a packaged classic desktop executable with AppContainer trust.
SunPlayer does not adopt it in this packaging slice because it is not a
documented Qt support configuration and its most important existing input path
is not broker-aware by construction:

```text
Qt Quick FileDialog
    -> Qt's Windows IFileOpenDialog backend
    -> local filesystem path
    -> FFmpeg avformat_open_input(path)
```

Microsoft's documented process-lifetime file grant applies to
`Windows.Storage.Pickers.FileOpenPicker`, file activation, and drag/drop. Qt's
Windows backend instead creates `CLSID_FileOpenDialog`; Microsoft does not
document that classic dialog as creating the same AppContainer grant. Even
after a brokered picker grant, the ordinary path-open behavior used by the
desktop FFmpeg build must be demonstrated rather than inferred from the
`StorageFile` contract.

A later bounded feasibility study can package the real install tree as
AppContainer and test Qt startup, QML/plugins, the native dialog, direct
FFmpeg input, WASAPI through cubeb, D3D11VA, QRhi/libplacebo, HDR observation,
settings, logging, fullscreen, and other-display blanking. It should change
the product only when a concrete incompatibility identifies a narrow seam.
The v1 Store package does not carry dual manifests or speculative broker/AVIO
code for that experiment.

## Store delivery findings

The Store accepts `.msix`, `.msixbundle`, `.msixupload`, and related AppX
formats. It recommends an upload wrapper because it can carry an `.appxsym`
with public PDBs for Partner Center health analysis. A plain unsigned x64
`.msix` is sufficient for the first package/certification path; public-symbol
production and `.msixupload` assembly are a later release improvement unless
crash analytics is made a launch requirement.

Development and Store signing are distinct:

* A locally installed MSIX needs a trusted certificate whose publisher matches
  the manifest. A generated development PFX is local secret material and must
  never be committed or uploaded as a build artifact.
* The Store package is produced unsigned. Microsoft signs accepted Store
  packages, so Store CI needs no signing certificate.
* Direct sideload distribution outside the Store would require its own
  production signing and update policy and is not implied by this plan.

The Windows App Certification Kit is deprecated and unmaintained, so it is not
part of the maintained release workflow. Partner Center's current certification
result is authoritative. Test the loose layout and a signed development package
on clean supported Windows installations, including
install, launch, media playback, update over an older version, uninstall, and
reinstall. Package inventory must contain no build/test artifacts, private
keys, private symbols, or undeclared runtime dependencies.

Start with manual Partner Center submission. Listing text, screenshots, age
rating, markets, pricing, legal declarations, capability justification, and
release timing are human product decisions. Automating `winapp store` or the
Store submission API is useful only after repeated releases demonstrate a
stable submission process. Before public availability, acquire the Store-signed
certified package through a private audience or package flight on a current
Store-capable Windows system; a development-certificate sideload does not test
Store delivery or updates.

## Feature scope consequences

The first package does not register media file associations. SunPlayer accepts
one optional local positional path, but packaged file activation,
single-instance/multi-instance behavior, and a curated supported-extension list
are not implemented. Registering every extension FFmpeg might decode would
overclaim the product and could create one new process/window per activation.

Execution aliases, startup tasks, background tasks, protocol handlers, network
streaming capabilities, media-library enumeration, and shell extensions are
also outside this slice. Add a manifest extension only with the corresponding
implemented lifecycle and regression coverage.

## Decision

Use this validated release boundary:

```text
Release build
    -> cmake --install into a fresh x64 staging prefix
    -> installed-tree dependency and --verify-qml checks
    -> pinned winapp run for loose-layout package-identity testing
    -> pinned winapp package with qualified-asset PRI for unsigned Store MSIX
    -> signed development-package verification
    -> manual Partner Center upload and certification
    -> private-audience or package-flight acquisition
```

Keep CMake/Qt authoritative for deployment, keep `winapp` a thin pinned
adapter, and keep Partner Center authoritative for Store identity,
certification, and publication.

## Primary sources

* [Qt QML deployment script](https://doc.qt.io/qt-6/qt-generate-deploy-qml-app-script.html)
* [Qt runtime dependency deployment](https://doc.qt.io/qt-6/qt-deploy-runtime-dependencies.html)
* [Microsoft: Windows App Development CLI](https://learn.microsoft.com/en-us/windows/apps/dev-tools/winapp-cli/)
* [Microsoft: `winapp` with C++ and CMake](https://learn.microsoft.com/en-us/windows/apps/dev-tools/winapp-cli/guides/cpp)
* [Microsoft: `winapp` CLI reference](https://learn.microsoft.com/en-us/windows/apps/dev-tools/winapp-cli/usage)
* [Microsoft: product identity values](https://learn.microsoft.com/en-us/windows/apps/publish/view-app-identity-details)
* [Microsoft: application manifest activation combinations](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-f-application)
* [Microsoft: target device family manifest fields](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-targetdevicefamily)
* [Microsoft: capability declarations](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/app-capability-declarations)
* [Microsoft: MSIX AppContainer apps](https://learn.microsoft.com/en-us/windows/msix/msix-container)
* [Microsoft: file access permissions](https://learn.microsoft.com/en-us/windows/apps/develop/files/file-access-permissions)
* [Microsoft: packaged-desktop registry behavior](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-behind-the-scenes#registry)
* [Microsoft: flexible registry virtualization](https://learn.microsoft.com/en-us/windows/msix/desktop/flexible-virtualization)
* [Qt 6.11.1 Windows native dialog implementation](https://github.com/qt/qtbase/blob/v6.11.1/src/plugins/platforms/windows/qwindowsdialoghelpers.cpp)
* [Microsoft: upload MSIX packages](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/upload-app-packages)
* [Microsoft: package and upload-file forms](https://learn.microsoft.com/en-us/windows/msix/package/packaging-uwp-apps)
* [Microsoft: MSIX package requirements](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-package-requirements)
* [Microsoft: MSIX certification](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-certification-process)
* [Microsoft: Windows App Certification Kit](https://learn.microsoft.com/en-us/windows/uwp/debug-test-perf/windows-app-certification-kit)
* [Microsoft: Windows 10 version 1809 servicing status](https://learn.microsoft.com/en-us/windows/release-health/status-windows-10-1809-and-windows-server-2019)
* [Microsoft: Windows 10 Enterprise LTSC 2019 lifecycle](https://learn.microsoft.com/en-us/lifecycle/products/windows-10-enterprise-ltsc-2019)
* [Microsoft: Store submission overview](https://learn.microsoft.com/en-us/windows/apps/publish/get-started)
