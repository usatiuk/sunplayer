---
name: ship-change
description: Ship a substantial project change through grounded research, plan review, implementation, validation, documentation, independent review, design reassessment, and clean handoff. Use for non-trivial implementation, refactoring, architecture, testing, or documentation where current code, real production practice, meaningful failure modes, and project bookkeeping must be reconciled.
---

# Ship Change

Use this as a flexible completion loop, not ceremony. Follow repository
instructions first. Scale, combine, reorder, or omit steps when judgement says
that produces a clearer and safer result.

## 1. Orient and protect the workspace

Read the applicable repository instructions, current plan, subsystem docs,
accepted decisions, implementation, tests, and pinned dependency versions.
Inspect worktree state before editing and preserve user or unrelated changes.
State the requested behavior, current behavior, important invariants, risks,
unknowns, and a concrete completion condition.

## 2. Research the real problem

Research only uncertainties that can change the design or validation. Start
with the current repository and exact pinned dependency source. For library,
platform, lifecycle, media, or architecture questions, also inspect how mature
production projects solve the same problem and their known regressions. Prefer
primary documentation and source over summaries.

Keep evidence categories distinct:

- Current project behavior.
- Documented contract.
- Source-confirmed implementation detail.
- Inference or policy choice.
- Experiment still required.

Do not promote a research note into project truth without reconciling it with
the current architecture. Delegate bounded independent questions only when it
materially improves confidence or speed; keep tightly coupled work local.

## 3. Shape and challenge the plan

For substantial multi-step work, create or select a durable execution plan at
the repository's documented plan location before implementation begins. A
draft may retain unresolved research questions; mark it active only when it is
decision-complete enough that an implementer does not need to invent product or
architecture choices. Trivial edits and mechanical fixes do not need a plan
file merely to satisfy process.

Define the smallest coherent vertical slices, their behavioral acceptance,
validation, documentation, and useful commit boundaries. Review the plan with
the same judgement as code: remove speculative abstractions, redundant states,
unnecessary compatibility, and steps whose only purpose is to satisfy an
earlier plan. A separate plan-only commit is optional unless risk, duration,
review, or coordination makes that checkpoint useful.

Prefer latest-value reconciliation and eventual user-visible correctness for
capabilities, diagnostics, and transient platform state. Require strict
identity or ordering only for stale media, native-resource lifetime, clock
meaning, protocol-required asynchronous lifetime, or another demonstrated
invariant.

When a concrete axis already has materially different consumers, ownership,
or lifecycles, establish its narrow shared contract with the first
implementation. Do not add wrappers merely because something could vary.
Update the durable plan when evidence changes the design. Supersede it
explicitly rather than leaving two apparently active plans for the same
outcome.

## 4. Implement a coherent slice

Implement the behavior through the strongest existing boundary. Keep modules
cohesive and contracts narrow. Prefer deleting superseded paths and cruft over
leaving compatibility layers the project does not require. Do not broaden the
task into unrelated cleanup, but refactor an existing boundary when that is
the simpler way to make the requested behavior correct.

Keep the user informed during long work and surface assumptions that materially
affect scope or behavior.

## 5. Validate behavior

Validate at the highest practical public boundary with real dependencies and
representative data. Substitute clocks, devices, or OS state only where control
or determinism requires it. Prefer observable outcomes over private call
sequences and fixed sleeps.

For a significant bug fix, add a regression that fails without the fix when
practical; temporarily revert or disable the fix to prove that relationship
when doing so is safe and useful. Cover important failure, cancellation,
fallback, lifetime, and stale-work paths in proportion to risk. Follow the
repository's exact build and test instructions.

## 6. Synchronize project truth

Update the appropriate accepted architecture, subsystem plan, root progress,
decision record, research note, diagnostics contract, testing record, and
deferred-work entry. Preserve evidence and unresolved experiments without
presenting them as implemented behavior. Do not leave material reasoning only
in chat, a commit message, or a speculative architecture note.

## 7. Review the result

Self-review the complete diff for correctness, ownership, API use, concurrency,
lifetime, failure behavior, portability, cohesion, duplication, naming,
testability, observable fallback, documentation consistency, and unsupported
claims. Look specifically for abstractions without policy, wrappers without a
consumer, duplicated helpers, catch-all fallbacks, comments that restate code,
and tests that assert implementation calls instead of behavior.

Use independent read-only review in proportion to risk: none for trivial work,
one broad reviewer for a localized slice, and focused correctness/design/test
reviewers for consequential cross-subsystem or lifecycle changes. Triage every
material finding against evidence and intent, fix or explicitly defer it, then
rerun affected validation.

## 8. Stop and rethink when needed

Treat three review/fix rounds on the same design as a circuit breaker, not a
target. Stop earlier when clean. If material findings recur, question the
boundary, ownership, invariant, API, state model, and test seam. Prefer a
simpler redesign that removes the failure class over accumulating patches.

Continue iterating only for evidence-backed blockers such as wrong results,
crashes, hangs, data loss, security issues, lifetime/API violations, broken
core invariants, or misleading tests. Report missing authority, hardware, or
external state clearly rather than manufacturing certainty.

## 9. Finish cleanly

Run final relevant checks after review fixes. Inspect the final diff and status,
then update the execution plan with its actual outcome, validation evidence,
remaining gaps, and resulting commit subjects or links when useful. Summarize
the same material in the handoff. Commit, push, deploy, or publish only when
authorized; each is a separate authority unless repository or user
instructions explicitly combine them.
