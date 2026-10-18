# Separate CI and release orchestration

## Scope

Keep ordinary CI and owner-only manual releases as separate workflow graphs.
Reuse Windows and AppImage build jobs without copying their build recipes.
Prepare the exact version-bumped commit once, build both packages in parallel,
then attach them to a draft GitHub release. Keep existing version, artifact,
atomic ref push, and recovery checks. No Store submission or player changes.

## Implementation

- `ci.yml`: Linux checks and shared package builds; no release jobs or inputs.
- `build.yml`: reusable Windows and AppImage jobs, with no dependency between them.
- `release.yml`: preparation → shared parallel builds → draft publication.
- Preparation uploads its Git bundle independently of platform packages. Both
  builders verify the prepared SHA and version before building.
- Only publication receives write permission; CI and builders remain read-only.

## Verification

The preceding release run 39032416718 was cancelled at the user's request,
before creating v1.3.0. Validate workflow syntax and job dependencies, review
artifact handoff and rerun behavior, then commit/push on main and dispatch one
new minor release. Record actual checks and the replacement run below.

Local validation: actionlint 1.7.12 passes all three workflows (shellcheck is
unavailable locally). Three independent read-only reviews covered exact
revision/rerun correctness, permissions and workflow semantics, and KISS/DRY
scope. No blocking findings; draft labels and stale documentation were fixed.
Build recipes and existing version/ref recovery checks are otherwise retained.
PowerShell execution and cross-job artifact consumption await the hosted run.


## Development MSIX update versioning (2026-10-19)

Fixed development version 1.0.0.0 prevented reliable in-place updates. Use
`1.<run_number >> 16>.<run_number & 65535>.<run_attempt>` for development CI
packages only. Keep identity stable, use GitHub's existing counters, and retain
all field-range validation. A Development switch permits a nonzero revision;
Store callers keep the strict ending-in-zero requirement and release version.
No app version bump, dependency, platform runtime change or new counter service.

Validate counter carry/order boundaries locally, inspect the workflow/script
diff, then use hosted packaging and inspect the produced MSIX manifest version.
Native Windows/macOS builds remain prohibited on this Linux host. Installed
in-place update/data preservation is a user acceptance check.

Local checks parsed workflow YAML, verified the release caller omits Development,
and checked versions for runs 1, 65535, 65536, 65537 and 4294967295. Counter carry
and rerun ordering remain strictly increasing and all fields fit uint16. The
existing script validates the final version; uint32 conversion rejects counter
overflow. PowerShell execution/package validation is delegated to Windows CI;
no host dependency was installed and no native Windows build ran locally.
