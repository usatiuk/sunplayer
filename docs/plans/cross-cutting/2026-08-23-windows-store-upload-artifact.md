# Manual Windows release

Status: implemented and locally validated; hosted release pending.

## Grounding

The existing Windows CI job already owns the complete Qt/vcpkg build, install,
QML probe, and MSIX boundary. The release path should reuse that job rather than
copying it into a second workflow.

Partner Center accepts an `.msixupload` ZIP containing the MSIX and an optional
`.appxsym` ZIP. SunPlayer will put the full linker-produced `sunplayer.pdb` in
the APPXSYM. It will not discover test or third-party PDBs, put symbols in the
installed application, sign the package, or submit it to Partner Center.

The release commit must exist locally before CMake configures. Otherwise the
binary's Git-derived build ID would identify the parent commit while the tag
identified a later version commit. Remote refs are written only after the exact
release commit has built and packaged successfully.

Sources:

* <https://learn.microsoft.com/windows/apps/publish/publish-your-app/msix/upload-app-packages>
* <https://learn.microsoft.com/windows/msix/package/packaging-uwp-apps>
* <https://learn.microsoft.com/windows/win32/dxtecharts/debugging-with-symbols>
* <https://docs.github.com/actions/managing-workflow-runs/manually-running-a-workflow>

## Plan

1. Add a plain root `VERSION.txt` file containing strict `X.Y.Z` application
   version text. Make CMake read it as the project version and use the full
   version for macOS bundle metadata. Keep the QML module's unrelated import
   version at `1.0`.
2. Keep **CI** available for pull requests, main pushes, and an ordinary manual
   run. Expose the same workflow through `workflow_call`, then add a tiny
   dedicated **Release** workflow which accepts only `major`, `minor`, or
   `patch` and calls CI in release mode. Release mode runs only from `main` and
   requires both the initiating and re-running actor to be `usatiuk`; it is the
   only mode allowed to bump, tag, push, or publish.
3. From the dispatch's immutable `github.sha`, calculate the next version and
   Store version `X.Y.Z.0`. Reject zero Store major, components above 65535,
   missing Partner Center repository variables, moved `main`, or conflicting
   release refs. Starting from `0.1.0`, the first valid selection is `major`,
   producing `1.0.0` and `1.0.0.0`.
4. Update `VERSION.txt`, create the release commit and lightweight tag locally,
   and then configure/build. On a re-run after refs were already pushed,
   recover and build the already-tagged release commit only when the immutable
   tag, parent, and version match the original dispatch and that commit remains
   in `main` history. A later `main` commit does not invalidate release
   completion, while a separate duplicate dispatch does not use this recovery
   path.
5. Build the distribution as `RelWithDebInfo`, stage it, run packaged-QML
   verification, and create the reserved-identity x64 MSIX. Compress the exact
   nonempty linker output `sunplayer.pdb` alone at the APPXSYM root, then put
   the MSIX and APPXSYM alone at the MSIXUPLOAD root.
6. Upload the MSIXUPLOAD plus a git bundle containing the exact locally built
   release commit to a short-lived workflow artifact. The build job keeps
   read-only repository permission and persisted credentials disabled.
7. A dependent owner/ref-guarded job receives repository write permission.
   It verifies the bundle, fetches remote refs, and either atomically pushes the
   exact release commit to `main` with its tag or recognizes the matching refs
   from a previous partial run. It never forces or overwrites a conflicting
   ref.
8. Create the GitHub Release and attach the MSIXUPLOAD. If the release API or
   asset upload failed after refs were pushed, re-running the failed jobs
   resumes the same version and uploads a missing attachment instead of bumping
   again. An existing nonempty attachment is left unchanged. Partner Center
   submission remains a manual step.
9. Key the hosted vcpkg binary cache by the repository dependency inputs, the
   actual hosted vcpkg revision, and the runner image version. An older cache
   may seed a new ABI identity, but the new immutable cache is saved under a
   distinct key after a successful job instead of rebuilding forever behind an
   exact stale key.
10. Keep the shared Store package-version validation in the packaging wrapper
    and document only the operator-facing release procedure and remaining real
    release gates.

## Acceptance

* **Actions → CI → Run workflow** performs ordinary repository-read-only CI
  with no bump, tag, push, or GitHub Release.
* **Actions → Release → Run workflow** from `main` owns version bump, build,
  Store package, symbols, tag, atomic push, and GitHub Release.
* The built binary, checked-in `VERSION.txt`, release commit, tag, MSIX version,
  and GitHub Release all identify the same release.
* The MSIXUPLOAD root contains one MSIX and one APPXSYM; the APPXSYM root
  contains only the full nonempty `sunplayer.pdb`; the MSIX contains no PDB.
* Build or package failure, a moved `main` before the atomic push, a non-owner
  run/re-run, or a conflicting ref causes no remote mutation.
* Re-running after a successful atomic ref push but failed GitHub Release step
  resumes that exact release safely.
* Ordinary pull-request and main-push CI remains read-only and keeps its current
  development-package artifacts.

## Validation boundary

Local validation covers strict version parsing, CMake version propagation,
RelWithDebInfo symbol production, Store packaging, and both ZIP layouts. YAML
and the final diff receive independent review. The first hosted **Release**
invocation and first Partner Center acceptance remain unavoidable external
release gates; the repository does not claim them in advance. There is
deliberately no second dry-run mode: the first successful **Release** invocation
is the live `1.0.0` GitHub release, while any pre-publication failure leaves
remote refs unchanged.

On 2026-08-23, the valid and rejected version-bump matrix passed, including the
first-release `major` case and Store component overflows. An isolated shallow
Git checkout preserved the exact release commit through the bundle boundary. A
disposable bare remote passed fresh atomic publication, later-`main` recovery,
duplicate-attempt, reset-`main`, and conflicting-tag cases.
Actionlint 1.7.12 accepted both workflows. The Windows RelWithDebInfo
build completed and all 36 registered tests passed. A temporary reserved-
identity `1.0.0.0` package passed packaged-QML verification. Its MSIX had the
expected identity/version and no PDB; its APPXSYM contained only the complete
`sunplayer.pdb`; and its MSIXUPLOAD contained only that MSIX and APPXSYM. All
temporary probes were removed.
