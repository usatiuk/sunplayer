# Microsoft Store MSIX packaging

Status: Active — packaging pipeline implemented; first Store submission pending

## Goal

Produce a reproducible, certifiable x64 MSIX for SunPlayer from the existing
Windows Release install tree and complete the first controlled Microsoft Store
submission without changing the application's runtime architecture.

Completion requires:

* one reviewed Store manifest with stable Partner Center identity;
* one original product-logo source and complete reviewed MSIX asset set;
* an unsigned Store MSIX generated through a pinned packaging tool;
* a locally signed development package that installs, updates, launches,
  plays media, persists settings, and uninstalls cleanly on supported Windows;
* a reviewed package inventory and completed public-distribution obligations;
* successful Partner Center acceptance and controlled Store acquisition; and
* synchronized build, release, and user-facing documentation.

Publication timing remains a product choice after certification and controlled
Store acquisition. The supporting evidence and tool comparison are recorded in
[the MSIX research note](../../research/2026-08-16-windows-msix-store-packaging.md).

## Remaining release gates

The packaging mechanics are implemented and validated with a throwaway
identity. An actual Partner Center submission must not invent these external
product inputs:

1. A Partner Center developer account and reserved product whose Product
   identity page supplies the exact package Name, Publisher, and
   PublisherDisplayName.
2. An approved public version. The current CMake project version is `0.1.0`,
   while the Store package's first component cannot be zero; the likely first
   public version is `1.0.0`/`1.0.0.0`, but that is a release decision.
3. Completion and approval of third-party notices, corresponding-source/build-
   recipe delivery, LGPL source offers, FFmpeg/libass codec policy, and all
   redistribution terms already recorded by the build subsystem.

The original SunPlayer logo and complete MSIX asset set now live under
`packaging/windows/`. Partner identity, public version, and redistribution
approval gate only the real release artifact and submission; they do not block
maintaining or exercising the development pipeline.

## Grounded current state

* CMake already installs `bin/sunplayer.exe`, its toolset-matched app-local CRT,
  transitive vcpkg DLLs, Qt DLLs, production plugins, imported QML modules,
  `qt.conf`, and licenses into a self-contained Windows tree. Qt's separate
  redistributable installer and QML debugging plugins are excluded at the
  deployment source.
* The CI Windows job already makes and probes a clean Release install tree and
  uploads it for seven days on trusted events. That developer artifact is the
  future MSIX payload input, not a second deployment implementation.
* The package will be x64-only because that is the only implemented and
  validated Windows product architecture.
* `ApplicationSettings` uses normal Qt `QSettings`; no package-specific
  storage branch exists or is needed.
* SunPlayer opens local files selected by the current Qt Quick dialog or an
  optional positional path. File activation, drag/drop, and single-instance
  policy are deferred.
* Public binary distribution is currently prohibited by documented legal and
  redistribution gaps.

## Implemented design

The CMake-install boundary and medium-integrity MSIX policy are accepted.
`winapp` 0.6.0 with `Microsoft.Windows.SDK.BuildTools` 10.0.28000.2526 passed
the feasibility spike and is the pinned packaging adapter. It remains public
preview, so a version change requires the same loose, signed, and unsigned
package checks.

### Boundary and tool ownership

The normal Windows Release install remains the sole deployment boundary:

```text
CMake/Qt/vcpkg
    -> fresh cmake --install prefix
    -> installed-tree verification
    -> packaging/windows/Pack-WindowsStore.ps1
    -> pinned winapp and Windows SDK BuildTools
    -> unsigned x64 MSIX
```

Qt owns Qt/QML deployment, CMake owns the matching compiler runtime, and the
existing vcpkg install helper owns the other application-local DLL closure. The packaging integration owns only
manifest/workspace materialization, input validation, and `winapp` invocation.
It does not run `windeployqt`, discover DLLs, edit source manifests in place,
or teach CMake to call network/package tools during configure or build.

Do not add CPack, a Visual Studio packaging project, MSIX Packaging Tool
capture, custom MakeAppx orchestration, or a second install layout. A future
non-Store package can consume the same CMake install tree when that real need
exists.

### Checked-in source layout

```text
packaging/windows/
    Package.appxmanifest.in
    Pack-WindowsStore.ps1
    SunPlayer.rc.in
    StoreIdentity.json
    winapp.yaml
    README.md
    branding/
        SunPlayer.svg
    Assets/
        generated reviewed PNG/ICO variants
```

The exact pinned `winapp` version and archive hash live beside this packaging
entry point or directly in it; do not split one version across CI and local
scripts. `winapp.yaml` pins the exact `Microsoft.Windows.SDK.BuildTools`
package used for MakeAppx/MakePri and related SDK operations. The script accepts
the clean install prefix and an output directory, restores or validates those
exact tools, and fails closed on a version mismatch. It never contains or
generates a production signing secret.

The packaging script materializes a working manifest in a disposable sibling
workspace from the checked-in template and committed `Assets/` subtree, then
publishes the requested output directory only after validation succeeds. Store
mode reads the exact Partner Center identity only from reviewed
`StoreIdentity.json` and derives `major.minor.patch.0` only from CMake's project
version, then verifies the installed executable's Windows ProductVersion has
that same value; neither can drift through CI inputs or a stale install prefix.
Development/loose modes use one
clearly non-production identity. The script validates all values, never edits
its source template or assets, and uses the exact nested `bin/sunplayer.exe`
path for both loose and packed commands. Manifest materialization fails unless
all four version components are integers in `0..65535`, the first is nonzero,
and the fourth is zero.

Generate the asset set from the approved SVG with the pinned
`winapp manifest update-assets`, then commit and review the outputs. Ordinary
packaging validates their presence but does not regenerate them.

### Manifest policy

The first manifest has:

* exact Partner Center `Identity/Name`, `Identity/Publisher`, and
  `Properties/PublisherDisplayName` values;
* stable package-relative application ID `SunPlayer` (`Application/@Id`),
  frozen before the first Store publication because it participates in the
  AUMID;
* consistent user-facing `SunPlayer` capitalization;
* `Windows.Desktop` as the only target device family;
* Windows 10 build 17763 as the minimum, matching the current application/Qt
  technical floor rather than silently raising it for packaging;
* a reviewed `MaxVersionTested` equal to the newest Windows build used for that
  release's package validation, never a guessed or generator-default value;
* one `Package/Resources/Resource` with `Language="en-US"`, matching the
  current English-only product and intentionally omitted Qt translations;
* x64 architecture stamped by `winapp` from the executable;
* the literal nested executable path `bin\sunplayer.exe` in the materialized
  manifest plus the explicit executable argument where the pinned CLI requires
  it;
* `Windows.FullTrustApplication` as the packaged-classic entry point;
* the required `rescap:Capability Name="runFullTrust"`; and
* only visual metadata and assets needed for the desktop package.

This is normal non-elevated medium-integrity desktop execution. Do not add
`allowElevation`, `unvirtualizedResources`, `broadFileSystemAccess`, media-
library, network, device, background, startup, protocol, or alias capabilities.

Do not add file associations in this slice. They require a curated extension
contract, packaged activation handling, and a deliberate instance policy. Do
not infer that every format exposed by the packaged FFmpeg build is a product-
supported shell association.

AppContainer is not a v1 manifest variant. Its Qt dialog/direct-FFmpeg path,
audio, graphics, display, storage, and lifecycle compatibility remain a later
bounded security feasibility study. This plan adds no brokered picker, custom
AVIO, dual manifest, or AppContainer fallback.

### Signing and secret handling

Support three explicit package outputs:

* **Development MSIX:** packed with a locally generated certificate whose
  publisher matches the development manifest. The PFX/password remain outside
  Git and CI artifacts. Trust it only on controlled test machines, and remove
  loose registrations before installed-package tests.
* **Unsigned development MSIX:** uses the same clearly non-production identity
  without a certificate to exercise the unsigned package mechanics before a
  Partner Center product exists. It is never a Store submission artifact.
* **Store MSIX:** packed without a certificate and uploaded unsigned. Microsoft
  signs it after certification. Store packaging and CI need no production
  signing secret.

Production sideloading, an `.appinstaller` update feed, Azure Trusted Signing,
and direct-download certificates are separate distribution products and are
not introduced speculatively.

### Release and submission

The existing trusted workflow now exposes Store packaging only through one
explicit manual-dispatch boolean. It reuses the
existing Windows Release configure/build/install/verification shape, then:

1. obtain and verify the pinned `winapp` archive and SDK BuildTools package;
2. materialize the release manifest from reviewed source identity/version;
3. produce the unsigned x64 MSIX from the fresh install prefix;
4. inspect the package and run automatable package checks;
5. upload the MSIX, manifest/tool provenance, and inventory as bounded internal
   release artifacts; and
6. require a human to upload and submit through Partner Center initially.

Do not publish packages from pull requests or give contributor code access to
release credentials. Do not call `winapp store` until repeated manual releases
show that submission automation removes real work without hiding listing,
policy, rating, market, or release choices.

Partner Center accepts a plain `.msix`; use that for the first end-to-end
submission. Add public-PDB production plus `.appxsym`/`.msixupload` only when a
reviewed symbol-redaction and crash-analysis workflow exists.

## Implementation slices

### 0. Packaging-tool feasibility spike

* [x] Select one candidate `winapp` release and exact SDK BuildTools version.
* [x] Outside Git, create a throwaway manifest with a non-Store identity,
  placeholder assets, stable test application ID, and valid nonzero package
  version.
* [x] Build a fresh real SunPlayer install tree, create the complete temporary
  manifest/asset workspace, and prove the nested `bin\sunplayer.exe` path with
  loose registration and launch.
* [x] Pack and locally sign an x64 development MSIX, install and launch it, and
  inspect the payload before cleaning up the registration, package, certificate,
  scratch identity, and assets.
* [x] Record the candidate CLI/archive hash, SDK BuildTools version, command
  contract, and any incompatibilities. Accept `winapp` only if this slice passes
  without a project-owned MakeAppx implementation.

This is a bounded adapter decision, not a public-package rehearsal. It does not
wait for Partner identity, release branding, legal approval, or a new project
version, and it commits no scratch package input.

Evidence from the 2026-08-16 Windows build-26200 feasibility run:

* `winapp` 0.6.0 resolved MakeAppx 10.0.28000.2526 from the pinned
  `winapp.yaml` and materialized the explicit nested executable correctly;
* a clean `Release` configure/build/install completed, and the direct
  installed-tree QML probe plus loose-package QML probe passed;
* the existing real fullscreen/playback/default-audio scenario passed under
  loose package identity and the identity unregistered on exit;
* unsigned development packing, unpacking, manifest checks, inventory hashing,
  and absence-of-signature checks passed;
* a signed 1.0.0.0 development MSIX passed SignTool verification with zero
  warnings/errors, installed, and cold-launched; a stable-
  identity 1.0.1.0 update installed and cold-launched, and the package then
  uninstalled; and
* the one temporary certificate was removed from LocalMachine TrustedPeople by
  exact thumbprint, leaving no development package, certificate, or process.

### 1. Release prerequisites

* [ ] Reserve the Partner Center MSIX/PWA product and commit the exact Store
  identity values without secrets to `StoreIdentity.json`.
* [ ] Choose and apply the first public `PROJECT_VERSION`, with a matching
  `major.minor.patch.0` Store version whose components are within `0..65535`,
  whose first component is nonzero, and whose fourth component is zero.
* [x] Add an original project-owned SunPlayer logo and review the generated
  MSIX scale, target-size, and ICO assets.
* [ ] Complete and approve the public-distribution notice, source-offer,
  build-recipe, codec-policy, and redistribution workflow.

No public package may proceed past local experimental packaging while this
slice is incomplete.

### 2. Reproducible package inputs

* [x] Pin and checksum the tested `winapp` release and pin its exact SDK
  BuildTools package in `winapp.yaml`.
* [x] Generate a manifest scaffold once, replace generated identity with strict
  source-controlled Partner values, freeze `Application/@Id` as `SunPlayer`, set the reviewed
  min/max OS versions, `en-US` resource language, and nested executable/entry
  point, reduce it to the accepted manifest policy, and commit it as a
  version-configured template.
* [x] Generate, inspect, and commit the complete MSIX assets from the approved
  SVG using the pinned tool.
* [x] Add the small PowerShell wrapper and packaging README with local loose-
  layout, development-package, cleanup, and unsigned Store commands. Exercise
  the exact nested-executable contract proven by slice 0.
* [x] Keep development certificates, PFX passwords, package outputs, and loose-
  registration state out of Git.

### 3. Local package verification

* [x] Produce a fresh Release install prefix and rerun its existing dependency
  exclusions plus `--verify-qml` before packaging.
* [x] Use the pinned `winapp run` contract against the complete temporary
  package workspace and confirm the nested executable starts, deployed
  QML/plugins resolve, and the existing production playback/fullscreen/audio
  smoke passes under package identity.
* [x] Pack a signed development MSIX, remove the loose registration, install
  and cold-launch it on the current Windows 11 host, update 1.0.0.0 to 1.0.1.0
  with stable identity, then uninstall the package and exact temporary cert.
* [ ] Repeat installed-package launch/playback on a clean current Windows 11
  system. If the product retains build 17763 as a supported floor, also test a
  fully patched Windows 10 Enterprise LTSC 2019 system; otherwise raise the
  declared product/package floor rather than claiming an unsupported generic
  1809 SKU.
* [ ] Modify both packaged settings, relaunch, update to a higher package
  version with stable identity, and verify they persist. Fully uninstall and
  reinstall to observe and document the package-owned state lifecycle without
  adding `unvirtualizedResources`.

### 4. Package and certification evidence

* [x] Unpack every produced MSIX and validate the manifest and x64 architecture,
  required runtime closure, app-local CRT, qualified-asset PRI data, asset
  inventory, signed/unsigned container policy, and absence of redistributable
  installers, Qt QML debugging plugins, private symbols, certificates, and
  excluded D3D/DXC compiler DLLs.
* [ ] Complete clean-machine dependency inspection and reject any remaining
  unexpected DLL, observable absolute build path, or write expected beside the
  executable.
* [ ] Confirm required license/notice/source-offer material is present in the
  approved package or linked distribution channel and matches the exact
  shipped dependency graph.
* [ ] Confirm the clean current-Windows test system has no Qt SDK, Visual
  Studio, vcpkg, or repository checkout and repeat install, launch, upgrade,
  uninstall, and reinstall there rather than creating a duplicate smoke lane.

### 5. Controlled Store release

* [x] Add the trusted manual workflow after the local package is accepted.
* [x] Produce the unsigned MSIX and internal provenance/evidence artifacts
  without signing secrets.
* [ ] Complete the Partner Center listing, screenshots, category, age rating,
  markets/pricing, privacy/legal declarations, certification notes, and
  restricted `runFullTrust` explanation.
* [ ] Upload manually, resolve preprocessing/certification findings in the
  manifest/package source rather than editing a produced archive, and record
  Partner Center acceptance evidence.
* [ ] Acquire the Store-signed certified package through a private audience or
  package flight on a current Store-capable Windows system, then verify install,
  launch, version/identity, playback smoke, and Store-delivered update. A
  development-certificate sideload is not equivalent evidence.
* [ ] Choose publication hold/flight/rollout behavior explicitly before making
  the first certified package public.

## Acceptance and validation matrix

| Boundary | Evidence or policy |
| --- | --- |
| Tool feasibility | Pinned `winapp` 0.6.0 and SDK BuildTools 10.0.28000.2526 package, sign, install, update, and launch a throwaway x64 package from the real install tree |
| Install tree | Fresh Release prefix; dependency exclusions; relative `qt.conf`; installed `--verify-qml` pass |
| Manifest/workspace | Exact Store identity; frozen `Application/@Id`; bounded version; reviewed min/max OS versions; `en-US` resource; nested executable; medium-integrity entry point; only required capability/assets; complete sibling `Assets/` tree |
| Loose layout | Pinned `winapp run` contract launches `bin\sunplayer.exe` from the complete workspace; deployed runtime and writable locations work |
| Development MSIX | Local install and cold launch pass on Windows 11 build 26200; loose identity passes existing playback/fullscreen/audio smoke; clean-machine repetition remains open |
| Upgrade | 1.0.1.0 replaces 1.0.0.0 with stable package family and cold-launches; packaged settings mutation/preservation remains open |
| Uninstall/reinstall | Open release gate: observe and document package-owned state behavior; user media must remain untouched |
| Inventory | Only production payload, approved assets, and required legal material; no secrets/test/private-symbol/build-tree leakage |
| Store certification | Partner Center accepts the unsigned package and its identity, application ID, version, architecture, capabilities, and target-family min/max versions |
| Store delivery | Store-signed package is acquired through a private audience/flight on current Windows; install, launch, playback, identity/version, and Store update pass |

Package tests supplement rather than replace the existing Debug test suite,
QML lint, Release install probe, and physical Windows GPU/audio/fullscreen
evidence. All CMake, packaging, and installed-application commands must
run outside the workspace sandbox in a Visual Studio developer environment, in
accordance with repository instructions.

## Deliberately deferred

* AppContainer or a sandboxed media worker.
* File associations, packaged file activation, and single-instance routing.
* ARM64 and multi-architecture `.msixbundle` output.
* Public-symbol sanitization, `.appxsym`, and `.msixupload` assembly.
* Automated Partner Center submission or `winapp store`.
* Direct sideload distribution, production code-signing infrastructure,
  `.appinstaller`, winget, portable ZIP, MSI, or EXE installers.
* Network-stream capabilities, media-library enumeration, execution aliases,
  startup/background tasks, shell extensions, or other unimplemented product
  integrations.
* A generic cross-platform packaging abstraction before macOS and Linux define
  their own real distribution consumers.

## Documentation impact

Implementation must synchronize:

* `docs/subsystems/build/README.md` with the actual package boundary, pinned
  tool, manifest policy, payload, and verification evidence;
* `docs/subsystems/testing/README.md` with packaged-runtime coverage;
* `docs/DEFERRED.md` as packaging and legal gates close or change;
* root `PLAN.md` when Windows packaging is actually complete;
* application/settings documentation with observed MSIX upgrade/uninstall
  behavior; and
* a packaging README containing reproducible local developer commands and
  certificate cleanup instructions.

Store listing, privacy, licenses/notices, and user-facing installation support
material must describe the actually submitted package rather than this plan.

## Useful commit boundaries

1. Add the reviewed manifest template, approved branding/assets, tool pin, and
   deterministic local packaging entry point.
2. Add installed-package verification and record clean-machine evidence.
3. Add the trusted release workflow and internal evidence artifacts.
4. Synchronize release documentation after Partner Center acceptance.

Do not force these boundaries if resolving certification feedback produces a
smaller coherent change, and do not commit generated certificates or package
outputs at any boundary.

## Delivery evidence

Local development delivery evidence is recorded in slice 0 and the testing
subsystem. No Partner Center delivery evidence exists yet. The remaining
external gates are the reserved product identity, approved public version,
redistribution approval, clean-machine/package-floor validation, certification,
and controlled Store acquisition.
