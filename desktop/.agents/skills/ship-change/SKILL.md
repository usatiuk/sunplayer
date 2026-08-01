---
name: ship-change
description: Ship substantial project changes through grounded research, planning, implementation, validation, documentation, independent review, reassessment, and clean handoff. Use judgment to scale the workflow to risk and complexity; trivial or obvious changes do not require ceremony.
---

# Ship Change

Repository instructions, user intent, and sound judgment take precedence over
this workflow. Use the parts that improve confidence; do not create process
work with no product value.

## Workflow

1. **Ground the work:** Inspect current behavior, relevant code/tests/docs,
   worktree state, and pinned sources. Research primary sources, production
   practice, and known failures when they can change the design.
2. **Plan proportionally:** Keep a durable plan for substantial, risky, or
   multi-step work. Tiny, obvious, or mechanical changes may use a lightweight
   in-chat plan or no explicit plan.
3. **Implement coherently:** Use the strongest existing boundary, keep
   ownership narrow, remove superseded cruft, and preserve unrelated changes.
4. **Validate and document:** Prefer observable behavior at the highest useful
   boundary, meaningful regressions, and synchronized project truth.
5. **Review proportionally:** Self-review every diff. Scope, risk, uncertainty,
   and recurring problems are specifically reasons to use the independent
   multi-lens review loop below rather than an ad-hoc single pass.
6. **Finish cleanly:** Rerun affected checks, inspect final diff/status, update
   durable plans with actual evidence, and hand off concisely. Commit, push,
   deploy, or publish only when authorized.

## Independent review

When a review loop is warranted, apply the same method to the plan and/or
implementation as appropriate:

- Use at least three distinct read-only reviewer agents with independent
  lenses; three is a minimum, not a maximum.
- Cover behavior/correctness, architecture/failure risk, and
  tests/evidence/docs/scope. Add focused lenses when useful.
- Give reviewers raw artifacts without steering conclusions. Self-review does
  not count as an independent lens.
- Before editing, tell the user which findings will be fixed, deferred, or
  rejected. Keep decisions, changes, validation, and blockers visible while
  working.
- Resolve findings with evidence. If problems recur or expose a weak boundary,
  rethink the design instead of stacking patches.
- Repeat independent review after substantive review-driven changes when it
  materially improves confidence. Final evidence-only bookkeeping does not
  start another loop.
