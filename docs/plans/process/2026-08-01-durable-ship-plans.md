# Durable ship-change plan files

Status: Complete

## Goal

Give substantial project changes a durable, decision-complete execution plan
that remains useful after implementation, without turning small fixes into
process ceremony or duplicating subsystem roadmaps.

## Grounded current state

* `AGENTS.md` defines the roles of root and subsystem plans, decisions,
  research, and deferred-work records.
* The `ship-change` skill currently asks for a short working plan but does not
  require that plan to exist in the repository.
* Subsystem `PLAN.md` files describe ongoing roadmaps rather than one concrete
  implementation effort.

## Decisions

* Store execution plans under
  `docs/plans/<subsystem>/<YYYY-MM-DD>-<slug>.md`.
* Use `docs/plans/cross-cutting/` for work spanning several product
  subsystems and `docs/plans/process/` for repository workflow changes.
* Retain completed and superseded plans as project history.
* Require a tracked plan for substantial multi-step `ship-change` work, but
  not for trivial edits or mechanical fixes.
* Do not require a separate plan-only commit. Use one when risk, duration, or
  coordination makes the checkpoint useful.

## Implementation

1. Document placement, status, required content, lifecycle, and relationship
   to existing project documents in `docs/plans/README.md`.
2. Update `AGENTS.md` so execution plans are an explicit source of truth and
   part of the substantial-change workflow.
3. Update `ship-change` to create or select a tracked plan before
   implementation, require `Active` plans to be decision-complete, and record
   final evidence when work completes.
4. Review all three documents for compatible language and unnecessary
   ceremony.

## Acceptance and validation

* Existing root and subsystem `PLAN.md` roles remain unchanged.
* `Draft`, `Active`, `Complete`, and `Superseded` have unambiguous meanings.
* The workflow permits research to change a draft plan before implementation.
* Small fixes do not require a plan file.
* No plan-management tooling, generated index, or schema is introduced.
* Documentation links resolve and the final worktree contains only the
  intended documentation and skill changes.

## Commit boundary

Ship the plan convention, first process plan, repository guidance, and skill
update as one coherent documentation/workflow commit.

## Outcome

The repository now has a durable execution-plan convention, this first process
plan, matching repository guidance, and a `ship-change` workflow that requires
decision-complete tracked plans only for substantial multi-step work.

## Validation performed

* `git diff --check` passed.
* Required root, subsystem, plan-index, and process-plan paths were confirmed.
* A focused search confirmed that `AGENTS.md`, `ship-change`, and this plan use
  the same location and lifecycle terminology.
* No build or CTest run was needed because the change affects documentation and
  agent workflow only.

## Remaining gaps

None. The convention intentionally has no schema validator, generated index,
or plan-management tooling.

## Resulting commit

`Document durable ship-change plans` (the commit containing this plan).
