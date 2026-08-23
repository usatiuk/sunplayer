# Generate third-party notices without a packaged inventory

Status: Implemented and validated.

## Question

What is the smallest Windows packaging boundary that provides complete
third-party notices and rejects an unowned deployed runtime?

## Findings

The application consumes only `ThirdPartyNotices.txt`. `SupportController`
loads that text and shows it in About under **Third-party licenses**.

`ThirdPartyInventory.json` has no application, Qt, vcpkg, MSIX, Microsoft
Store, or licensing consumer. It was introduced solely so SunPlayer could
recheck paths and hashes after notice generation. Its schema, generator hash,
provenance hash, artifact hashes, mutation probes, and self-verification mode
form a custom package-attestation system. That is outside the product need.

The useful validation already happens at the point where notices are
generated. The Windows install step has all authoritative inputs available:

* vcpkg's installed port SPDX and copyright files;
* Qt's installed module SPDX, SDK runtime files, and source `LICENSES` files;
* Lucide's colocated README and license; and
* the final deployed DLL/EXE tree.

A grounded probe maps all 94 current dependency runtimes uniquely: 78 Qt and
16 vcpkg runtimes, with zero unknown or ambiguous files. Matching deployed
paths to those metadata-owned paths is enough to reject an unowned or
ambiguous runtime before installation completes. Qt, vcpkg, and CMake remain
responsible for deploying the required closure. Nothing needs to preserve the
mapping in the shipped application.

`winapp` packages and validates the completed install tree. Microsoft package
signing, rather than a SunPlayer JSON ledger or a second archive inspector,
owns post-packaging integrity.

## Decision

Keep one Windows-only notice generator. It validates every deployed dependency
runtime against vcpkg/Qt metadata and writes one human-readable artifact:

```text
share/sunplayer/ThirdPartyNotices.txt
```

Do not generate or ship a machine inventory. Do not retain a schema,
provenance hash, generator hash, checksum layer, file-hash ledger,
`-VerifyOnly` mode, notice deduplication, runtime listings, or tests for
mutating removed state.

The Store wrapper requires the executable, SunPlayer license, privacy policy,
and third-party notices, then invokes `winapp`. About continues to show only
the generated notice text. The checked-in MSIX manifest remains the authority
for Windows 11 24H2 and the Store-serviced VCLibs dependency.

## Practical boundary

This intentionally does not defend against someone modifying a staged install
after CMake installation and before invoking the package wrapper. That was a
speculative failure mode which created the inventory machinery. Normal
packaging uses the freshly generated install artifact; CI validates that same
artifact and the resulting MSIX.

## Evidence

On 2026-08-23 the RelWithDebInfo build passed both QML lint targets and all 36
registered CTests. A fresh install generated 828 KB of plain-text notices for
21 components while resolving all 94 dependency runtimes, produced no JSON,
and passed packaged-QML verification. A disposable copied-install probe proved
that an unowned DLL stops generation. The unsigned Store MSIX then built
successfully from that install.
