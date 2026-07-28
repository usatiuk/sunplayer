# AGENTS.md

This project is a cross-platform HDR video player built around Qt, QRhi, FFmpeg, libplacebo, and libass.

Use your own judgement when designing and implementing solutions. Prefer simple, coherent designs over rigid adherence to illustrative architecture notes.

## Project context

Before substantial work, read:

1. `PLAN.md`
2. Relevant subsystem documentation under `docs/subsystems/`
3. Relevant decisions under `docs/decisions/`
4. `docs/ARCHITECTURE_NOTES.md` when broader technical context is needed

`docs/ARCHITECTURE_NOTES.md` contains research, possibilities, and illustrative designs. It is not a binding specification.

## Documentation discipline

Keep project documentation synchronized with the implementation.

When work changes architecture, interfaces, invariants, platform assumptions, supported behavior, or project status:

* Update the relevant subsystem documentation.
* Update `PLAN.md` when progress or scope changes.
* Record significant architectural decisions under `docs/decisions/`.
* Record useful investigation results under `docs/research/`.
* Record important deferred work, limitations, and discovered issues in `docs/DEFERRED.md`.

Do not leave important reasoning only in chat transcripts, temporary notes, or commit messages.

## Sources of truth

Use each document type for its intended purpose:

* `PLAN.md`: goals, scope, subsystem index, and high-level progress.
* Subsystem `README.md`: current accepted architecture and behavior.
* Subsystem `PLAN.md`: active subsystem work when additional detail is useful.
* Decision record: why a significant architectural choice was made.
* Research note: evidence, experiments, alternatives, and unresolved findings.
* `DEFERRED.md`: known work that is intentionally not being addressed yet.

Research notes and architecture notes are not automatically current project truth.

## Working principles

* Maximize shared cross-platform code and behavior.
* Keep unavoidable platform-specific behavior behind narrow interfaces.
* Prefer mature libraries and operating-system facilities over custom implementations where appropriate.
* Preserve modularity, testability, cancellation, and observable fallback behavior.
* Avoid unnecessary platform forks and speculative micro-optimizations.
* Keep user-facing behavior opinionated and simple even when internals are flexible.
* Do not derail current work to fix every incidental issue. Record relevant deferred findings instead.
* Make hardware-decoding, texture-copy, graphics-backend, and fallback behavior visible through diagnostics.

## Reviews

For substantial changes, verify that:

* Documentation and progress tracking remain current.
* New architectural decisions have been recorded where appropriate.
* Platform-specific logic has not unnecessarily leaked into shared modules.
* Known limitations and deferred issues are documented.
* Relevant tests and diagnostics exist or are tracked.
