# Productize card hover and Windows scrollbar contrast fixes

## Scope and evidence

The user approved both fixes after investigation. Linux CSD's full-window
HoverHandler prevents card hover delivery to lower siblings. The isolated card
works; the full shell reproduces failure; limiting that observer to titleBar
restores enter/leave and passes existing inactivity coverage. Scope the existing
observer, keeping PlayerPage's content observer and existing coordinate origin.

Windows Fluent's Fusion fallback scrollbar has insufficient dark contrast,
matching QTBUG-138957. Reuse stock ScrollBar with only Windows mid/dark roles
bound to its theme text color. Share this through one browser component used by
existing vertical/horizontal bars. Retain non-Windows palettes, layout, fade,
interaction and native scrolling. No Qt patch, global palette, dependency,
new setting or custom input handler. Other tracked Qt issues remain deferred.

## Validation and delivery

Add full-shell card hover enter/leave and press/leave coverage with CSD, using
existing HTTP fixtures. Check the shipped Windows scrollbar palette through
hosted shell coverage. Build Linux debug and lint; run Basic/Fusion shell and
packaged-QML checks; rebuild/smoke AppImage. Obtain independent correctness,
simplicity and tests/deployment reviews. Commit/push main only after local checks;
check CI and return the MSIX artifact. Native Windows/macOS execution stays in CI
or user acceptance, never this Linux host.

## Local delivery evidence

Implemented the one-line titleBar observer scope and a stock JellyfinScrollBar
with two Windows-only palette bindings, reused by four existing bars. Regression
coverage verifies card hover enter, press/leave/release, re-entry and exit with
CSD enabled. Windows checks readable roles; other platforms compare stock roles.

Linux debug build, Basic/Fusion shell tests, packaged-QML validation, all_qmllint
and rebuilt AppImage playback smoke passed. Three independent read-only reviews
covered correctness/lifecycle, simplicity and tests/deployment/docs. No runtime
blockers were found; stale investigation-only documentation was corrected. No
native Windows/macOS builds ran locally. Installed Windows contrast remains a
manual acceptance check. Temporary standalone investigation probes were removed.

The Qt issue index includes QTBUG-149927 and the verified mixed-DPI fix now in
dev/6.12 maintenance, separately from unverified released inclusion and the
independent hybrid-GPU variant. Portal appearance remains deferred.


## Shared control naming and coverage correction

The user requested application-wide ownership of the scrollbar policy. Rename
JellyfinScrollBar to AppScrollBar at the shared app root and use it at all six
explicit QML scrollbar attachment sites, including player details and HDR Lab.
The two Windows-only palette bindings are unchanged; QWidget scrollbars and
other platforms retain their stock behavior. Rename the existing palette test
and update production/test QML registration. No new API or rendering logic.

For this naming/coverage follow-up, Linux debug rebuild, Basic/Fusion shell
coverage, packaged-QML validation and qmllint all passed. Self-review confirmed
all six attachment sites use the shared component and its production/test QML
aliases match. No color or input behavior changed beyond the two additional
Windows scrollbar consumers.

## Hosted Windows diagnostic follow-up

Run 39168137309 built but all three shell-style tests failed without captured
output, alongside the existing decoder failure. Do not assume the shell failures
are flaky or change behavior without an assertion. Add standard QtTest JUnit XML
files alongside console output and print those reports on Windows CTest failure.
Linux Basic/Fusion tests pass with both loggers. An isolated Linux probe with the
palette bindings active also passes; this does not establish Windows behavior.
