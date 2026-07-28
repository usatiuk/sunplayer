---
name: ship-change
description: Ship a substantial project change through proportionate research, planning, implementation, validation, independent review, and design reassessment. Use for non-trivial implementation, refactoring, architecture, testing, or documentation slices where several concerns or meaningful failure modes must be reconciled before handoff or commit.
---

# Ship Change

Use this as a flexible completion loop, not a ceremony. Follow the repository's
instructions first and use judgement to scale or reorder the workflow.

## Work the slice

1. Orient in the current code, plans, decisions, and relevant documentation.
2. Identify the behavior, invariants, risks, unknowns, and a clear completion
   condition.
3. Research only meaningful unknowns. Delegate bounded, independent questions
   when that will improve confidence or speed; keep trivial and tightly coupled
   work local.
4. Maintain a short working plan when the work has multiple meaningful steps.
5. Implement the smallest coherent slice that proves the intended boundary.
6. Validate at the strongest practical boundary, including important failure
   and fallback paths. Prefer behavioral tests over implementation-call tests.
7. Synchronize documentation, decisions, progress, diagnostics, and deferred
   work when the slice changes them.

## Challenge the result

After the implementer's own checks pass, use independent review in proportion
to risk:

- Trivial edits usually need self-review and targeted validation only.
- A localized, well-tested change may need one reviewer covering several
  concerns.
- Cross-subsystem, lifecycle, concurrency, platform, media, color, or
  high-consequence changes may benefit from two or three focused reviewers.

Choose useful review lenses rather than maximizing comments:

- Correctness: APIs, lifetimes, concurrency, failure behavior, portability,
  security, and observable invariants.
- Design: ownership, module seams, cohesion, duplication, naming, testability,
  and appropriately modern patterns.
- Verification: meaningful regression coverage, diagnostics, fallback
  visibility, documentation, progress, and unsupported claims.

Keep reviewers read-only unless there is a clear reason to delegate a fix.
Triage findings against evidence and project intent; the implementer owns the
decision. Fix material findings, reject false positives with a reason, and
rerun affected validation.

Watch for AI-generated-code smells: speculative abstractions, wrappers without
policy, duplicated helpers, comments that restate syntax, catch-all fallbacks,
unsupported defensive checks, inconsistent naming, unrelated refactors, and
tests that assert calls instead of behavior. “Modern” means an appropriate,
established pattern—not novelty.

## Stop and rethink

Use three review/fix rounds for one design as a circuit breaker, not a target.
Stop earlier when the slice is clean.

If material findings persist or recur, reassess why:

- Is the boundary, ownership, invariant, API, or test seam wrong?
- Is complexity making invalid states or lifetime errors too easy?
- Would a simpler, stronger design remove a class of findings?

When a materially better design is credible, state the failure hypothesis,
reimplement, and begin a fresh review cycle. When no credible redesign exists,
continue only for evidence-backed blockers such as wrong results, crashes,
hangs, data loss, security issues, lifetime/API violations, broken core
invariants, or materially misleading tests. Do not loop on optional style churn.

If a blocker requires missing authority, information, hardware, or external
state, report it clearly instead of looping indefinitely.

## Finish

Run the final relevant checks after review fixes and summarize the evidence,
remaining gaps, and deferred work. Commit only when the task authorizes it.
Commit permission does not imply permission to push, deploy, or publish.
