# AGENTS.md

This project is a cross-platform HDR video player built around Qt, QRhi, FFmpeg, libplacebo, and libass.

The supported Linux desktop target is Wayland. X11 and XWayland presentation
are out of scope: do not add compatibility paths, fallbacks, packaging claims,
or test requirements for them unless a later architecture decision explicitly
changes that product scope. Linux audio and media work should still use the
appropriate native services available on supported Wayland systems.

Use your own judgement when designing and implementing solutions. Prefer simple, coherent designs over rigid adherence to illustrative architecture notes. The guidance in this file is a set of defaults, not a substitute for judgement; scale process and abstraction to the actual risk and complexity of the work.

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
* When accepted requirements make an axis of variation concrete and materially
  different implementations, ownership, or lifecycle models are already
  known, establish its narrow shared contract with the first implementation.
  Do not hard-code the first case and defer the seam until another
  implementation arrives.
* Shape those contracts from known consumers, ownership, synchronization,
  capability, fallback, and lifecycle requirements even when only one
  implementation can be exercised immediately.
* Keep modules cohesive, with narrow responsibilities and explicit contracts at subsystem boundaries.
* Make designs as simple as possible, but not simpler than correctness, lifecycle, recovery, platform behavior, and observability require.
* Prefer designs that converge quickly to the correct user-visible state over
  machinery that makes every transient frame, diagnostic snapshot, or event
  interleaving perfect. Add strict ordering, revisioning, and reconciliation
  only when stale state can cause wrong media, unsafe lifetimes, persistent
  incorrect output, or another demonstrated product failure.
* Treat diagnostics and non-critical capability observations as eventually
  consistent unless a concrete requirement needs an atomic snapshot. Do not
  turn best-effort library integration into a duplicate policy engine merely
  to cover theoretical interleavings.
* Prefer a small number of purposeful abstractions over layers introduced only
  for architectural symmetry.
* Prefer mature libraries and operating-system facilities over custom implementations where appropriate.
* Preserve modularity, testability, cancellation, and observable fallback behavior.
* Avoid unnecessary platform forks and speculative micro-optimizations.
* Keep user-facing behavior opinionated and simple even when internals are flexible.
* Do not derail current work to fix every incidental issue. Record relevant deferred findings instead.
* Make hardware-decoding, texture-copy, graphics-backend, and fallback behavior visible through diagnostics.

## Implementation workflow

For substantial implementation work, normally:

1. Orient from the current plan, relevant subsystem documentation, decisions, and implementation.
2. Investigate meaningful unknowns before committing to a design. When research, API verification, or codebase auditing can be separated cleanly, delegate bounded questions to one or more subagents and synthesize the results before implementation.
3. Create or update a short working plan. Keep it synchronized when discoveries change the approach or scope.
4. Implement the smallest coherent vertical slice that proves the relevant boundaries and behavior.
5. Validate in proportion to risk, including important failure and fallback paths, and synchronize the relevant documentation.
6. For a substantial or high-risk slice, use the project-local
   `$ship-change` skill when available before treating the work as complete.
7. Rerun affected validation after review fixes. When a task includes a commit, commit only after material findings are resolved, rejected with evidence, or explicitly deferred.

This flow is not ceremony or a hard gate. Trivial edits may not benefit from delegation or a written multi-step plan, while substantial documentation, audits, and test design can benefit when they contain separable questions or meaningful unknowns. Tightly coupled work may still be clearer when handled by one agent. Skip, combine, or reorder steps when that produces a clearer and safer result, and always use your own best judgement.

## Shipping substantial changes

The project-local `$ship-change` skill defines the reusable
research–plan–implement–validate–review–rethink loop. Apply it proportionally:
trivial edits normally need only self-review and targeted validation, while
cross-subsystem, lifecycle, concurrency, platform, media, or color-pipeline
changes benefit from independent review.

`AGENTS.md` remains authoritative for project principles, documentation,
testing, and local instructions. Review completion is not blanket
authorization to commit, push, deploy, or publish.

## Testing discipline

* Prefer regression tests through the highest practical public boundary, using real dependencies and representative data.
* Substitute clocks, devices, operating-system state, or other edges only when control or determinism is required for the behavior under test.
* Prefer observable completion events and state over fixed sleeps or private call-sequence assertions.
* Optimize for meaningful reproducible behaviors, not test count or a prescribed unit/integration ratio.
* Add a regression scenario for significant bug fixes when practical. Otherwise document why automation is not yet possible and what coverage remains missing.
* Keep verification strategy and known coverage gaps current in subsystem documentation and `docs/TESTING.md`.

## Review checklist

For substantial changes, verify that:

* Intended behavior, important invariants, ownership, and failure paths are
  correct and observable.
* Module boundaries remain narrow and the implementation is no more complex
  than the problem requires.
* Tests exercise meaningful behavior at the strongest practical boundary.
* Documentation and progress tracking remain current.
* New architectural decisions have been recorded where appropriate.
* Platform-specific logic has not unnecessarily leaked into shared modules.
* Known limitations and deferred issues are documented.
* Relevant tests and diagnostics exist or are tracked.
