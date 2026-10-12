# semicolon — R6 IDE superapp

Future builds belong to [b](https://github.com/vex-graph/b). No runnable editor
target exists yet.

## Current State

**Draft — not finalized.** Every contract below is a design goal.

**Role:** R6 IDE superapp: Language support, bounded `b` tool integration and
future Primeagen agent-client composition over R1–R5 public contracts.
Primeagen remains independently buildable R5, never dependent on Semicolon.

**Implemented and proven:** nothing. Repository documentation and license only; no tracked
`src`, no editor code and no test partition (`src/oop` and `src/cli` are
reserved and empty).

**Platforms proven:** none.

## What it is
`semicolon` composes IDE workflows: editing interfaces from Darling, syntax
from Language, build tooling through explicit bounded `b` invocation, and future
AI workflows through Primeagen's public consumer seam. No such composition is
implemented yet, and inclusion does not transfer lower-tier implementation ownership.

## Depends on (Vertical Integration Law allowlist)
Borrows public shapes and registered workflows from R1–R5 to build;
owns no OS/window/memory management itself. Standalone-capable or
Kernel-registered.

## Scope and Limitations

**Scope (intended):** R6 IDE workflow composition, Language support, bounded
`b` integration and future independent Primeagen R5 agent-client integration.

**Deliberately not covered:** no OS/window/memory management (borrowed from
R1–R5); no grammar engine of its own (`language` owns grammars); no lower-tier
contract is owned here. GPU shaders, dispatch and graphics composition remain
Graphvex R3, not a second superapp graphics implementation.

**Known limits and gaps:** this is an unfinished superapp/editor blueprint, not a finished IDE. R2 is
split between Vexspoke CPU computation/behavior and Relational Engine
memory/storage/native C search; migration is staged with the retained Vexspoke
ABI/default allocator. No C/Rust atomic-layout equivalence, automatic schema
migration or implemented app integration is implied. See the ecosystem readiness
Gist for granular scope and gaps.

## Layout
- `src/oop` — editor object model (reserved).
- `src/cli` — command-line entry surface (reserved).
- Tests (future): add a `tests/semicolon/` partition in the shared `../../tests` repo (mirrored
  per unit, the Test Tree Mirror Law); no test file lives inside this repo's
  source directories (the Test Segregation Law).

## Laws that govern work here
- Constitution: the [canonical preferences.md Gist](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a); real, Git-ignored `../../gist/PREFERENCES.md`, not a Vexspoke file or symlink. Lower tiers never include R6 headers or code.
- Commits land in THIS repo root, one cohesive unit each; never push unless asked.
- One public class per `.h`/`.c` pair, `(*ptr).field` (never `->`), dest-last
  params, `-Wall -Wextra -Werror`.
