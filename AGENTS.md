# AGENTS.md

This project is a cross-platform HDR video player built around Qt, QRhi, FFmpeg, libplacebo, and libass.

The supported Linux desktop target is Wayland. X11 and XWayland presentation
are out of scope: do not add compatibility paths, fallbacks, packaging claims,
or test requirements for them unless a later architecture decision explicitly
changes that product scope. Linux audio and media work should still use the
appropriate native services available on supported Wayland systems.

Use your own judgement when designing and implementing solutions. Prefer simple, coherent designs over rigid adherence to illustrative architecture notes. The guidance in this file is a set of defaults, not a substitute for judgement; scale process and abstraction to the actual risk and complexity of the work.

## Commit messages

Use an imperative subject and a detailed body explaining what changed, why,
alternatives considered, why the chosen approach was selected, known defects
or risks, and deferred work.

## Engineering contracts

* Express mandatory dependencies and lifetime/order contracts with types,
  references, and assertions.
* Never hide an impossible state or packaged-program defect behind a warning,
  sentinel value, ignored result, or inert fallback; fail fast.
* Recover only explicitly classified external failures, leaving a canonical
  state and a bounded retry trigger.
* Validate untrusted values once at their boundary, then trust the established
  invariant instead of repeating clamps and guards.
* Avoid duplicate state and speculative layers. Add an abstraction only when
  it enforces a real boundary or simplifies behavior that exists now; do not
  add fake platform implementations.
* Comment non-obvious invariants and reasons; do not narrate obvious code.

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
* `docs/plans/<subsystem>/`: durable execution plans for concrete substantial
  changes and their final delivery evidence.
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
  interleaving perfect. Compare the latest semantic value and reconcile it at
  a safe boundary by default. Add a generation, epoch, revision, or strict
  ordering rule only when it protects media identity, native-resource
  lifetime, clock meaning, a protocol-required asynchronous lifetime, or
  another demonstrated product invariant. Do not create identities merely to
  make capability observation or diagnostics appear atomic.
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

## Change workflow

For substantial implementation, refactoring, architecture, testing, or
documentation work, use the project-local `$ship-change` skill. It is the
shared research–plan–implement–validate–review–rethink–finish workflow,
including production-project research and multi-lens review when they improve
confidence. Apply it with judgment proportional to actual risk and complexity;
trivial or obvious edits do not require plans or reviewer subagents merely to
satisfy process.

`AGENTS.md` remains authoritative for project principles, documentation roles,
testing discipline, and local instructions. The skill does not grant authority
to commit, push, deploy, or publish.

## Testing discipline

* Prefer regression tests through the highest practical public boundary, using real dependencies and representative data.
* Substitute clocks, devices, operating-system state, or other edges only when control or determinism is required for the behavior under test.
* Prefer observable completion events and state over fixed sleeps or private call-sequence assertions.
* Optimize for meaningful reproducible behaviors, not test count or a prescribed unit/integration ratio.
* Add a regression scenario for significant bug fixes when practical. Otherwise document why automation is not yet possible and what coverage remains missing.
* Keep verification strategy and known coverage gaps current in subsystem documentation and `docs/TESTING.md`.
