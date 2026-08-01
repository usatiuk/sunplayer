# Execution plans

This directory contains durable plans for concrete implementation efforts.
They preserve the intended outcome, grounded design, acceptance criteria, and
final evidence for substantial work without replacing project roadmaps.

## Placement

Use:

```text
docs/plans/<subsystem>/<YYYY-MM-DD>-<slug>.md
```

Choose the subsystem that owns the primary outcome. Use `cross-cutting/` when
several product subsystems share ownership and `process/` for repository or
development-workflow changes. Update an existing plan when work is a
continuation of the same outcome instead of creating overlapping plans.

## Status

Every plan declares one status near its title:

* `Draft`: research or material decisions remain open. Implementation has not
  started.
* `Active`: the plan is decision-complete enough to implement without inventing
  product or architecture choices.
* `Complete`: the intended outcome has shipped and the plan records actual
  validation and any remaining gaps.
* `Superseded`: another linked plan or accepted decision replaced this plan
  before completion.

Plans remain in the repository after completion or supersession.

## Content

Keep a plan proportional to the work, but include enough information to hand
it to another engineer or agent:

* Goal and behavioral completion condition.
* Grounded current behavior and relevant evidence.
* Chosen approach, important invariants, and explicit non-goals.
* Coherent implementation slices and affected subsystem boundaries.
* Behavioral tests, failure scenarios, and other validation.
* Documentation impact and useful commit boundaries.
* Unresolved blockers while `Draft`, or remaining gaps after completion.

Research with lasting value may live under `docs/research/`; the plan should
link it and state the decision drawn from it. Do not copy a research dump into
the plan.

## Lifecycle

For substantial multi-step work using `ship-change`, create or select the plan
before implementation begins. Research may start in a `Draft` plan. Review and
simplify it before moving to `Active`, then keep it synchronized when evidence
changes the design.

On completion, record the actual outcome, validation performed, remaining
gaps, and resulting commit subjects or links when useful. A separate plan-only
commit is optional; use one when risk, duration, review, or coordination makes
the checkpoint valuable.

Trivial edits, mechanical changes, and small localized bug fixes do not need a
plan file merely to satisfy process.

## Relationship to other documents

* Root `PLAN.md`: project goals, scope, subsystem index, and high-level
  progress.
* Subsystem `PLAN.md`: the subsystem's active roadmap.
* Subsystem `README.md`: accepted current architecture and behavior.
* Execution plan: one concrete change and its delivery evidence.
* Decision record: why a significant architectural choice was accepted.
* Research note: evidence, alternatives, and unresolved experiments.
