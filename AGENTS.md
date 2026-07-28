# Commit Messages

Use an imperative subject and a detailed body explaining what changed, why, alternatives considered, why the chosen approach was selected, known defects or risks, and deferred work.

# Engineering

- Express mandatory dependencies and lifetime/order contracts with types, references, and assertions.
- Never hide an impossible state or packaged-program defect behind a warning, sentinel value, ignored result, or inert fallback; fail fast.
- Recover only explicitly classified external failures, leaving a canonical state and a bounded retry trigger.
- Validate untrusted values once at their boundary, then trust the established invariant instead of repeating clamps and guards.
- Avoid duplicate state and speculative layers. Add an abstraction only when it enforces a real boundary or simplifies behavior that exists now; do not add fake platform implementations.
- Comment non-obvious invariants and reasons; do not narrate obvious code.
