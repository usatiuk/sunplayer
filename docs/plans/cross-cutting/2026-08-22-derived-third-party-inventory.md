# Simplify Windows third-party notices

Status: Implemented and validated.

Grounding is recorded in
[the research note](../../research/2026-08-22-derived-third-party-inventory.md).

## Goal

Generate exact third-party notice text from the dependencies actually deployed
on Windows and fail installation for an unowned runtime, without shipping or
maintaining a custom machine inventory.

## Delete first

* Delete `ThirdPartyInventory.json` generation and packaging.
* Delete the inventory schema, generator/provenance/artifact hashes, and file
  records retained only for later comparison.
* Delete SPDX checksum attestation and final/source byte comparisons.
* Delete `-VerifyOnly` and the Store wrapper's inventory verification call.
* Delete the custom packaging test and its provenance/runtime mutation probes.
* Delete runtime path listings and notice deduplication from the human text.
* Delete inventory-focused documentation and validation claims.

## Keep

* One Windows-only generator using installed vcpkg and Qt metadata.
* Final-runtime ownership by exact installed-metadata path.
* Rejection of unowned or ambiguous deployed DLL/EXE files while generating
  notices.
* Dependency-owned notice text, exact versions, and source locations in
  `ThirdPartyNotices.txt`.
* The existing About **Third-party licenses** text page.
* The checked-in Windows 11 24H2 and Store-serviced VCLibs manifest contract.

## Implementation

1. Rename the script to `Generate-ThirdPartyNotices.ps1` and reduce it to a
   single generation mode.
2. Retain vcpkg/Qt/Lucide metadata discovery and exact-path runtime ownership;
   emit only component/version/source plus verbatim notice text.
3. Update the CMake install hook and Store wrapper. The wrapper checks that its
   required input files exist and then packages them.
4. Delete the custom packaging test and its CI calls; successful install and
   `winapp` packaging are the publishing gates.
5. Preserve the now-concise README/privacy language and remove stale inventory
   terminology from current packaging/subsystem documentation.

## Validation

* Parse all changed PowerShell scripts and run `git diff --check`.
* Generate notices from a fresh Windows install and confirm all 94 current
  dependency runtimes resolve with no inventory file produced.
* Run the registered Windows tests and both QML lint targets.
* Build an unsigned MSIX successfully from the fresh install.
* Run three independent final reviews, including an explicit simplicity lens.

## Checklist

- [x] Research and correction plan written before implementation.
- [x] Inventory JSON and all supporting machinery removed.
- [x] Notice generation retains actual-runtime ownership validation.
- [x] Store wrapper is reduced and the custom MSIX test is deleted.
- [x] README/privacy and subsystem documentation are concise and accurate.
- [x] Fresh install, test, QML, and MSIX validation pass.
- [x] Three final reviewers report no material findings.

## Non-goals

No package attestation, custom SBOM, shipped hash manifest, dependency database,
network lookup, license classifier, or new abstraction layer.
