# semicolon — R5 mini IDE

## CLion: CMake is IDE metadata only

Open this repository root as a CMake project. `CMakeLists.txt` is an IDE-only
blueprint entry: there are no production sources or C23 source targets yet,
so there is nothing to provide semantic diagnostics or inlay hints for.
No fake declarations, dependency downloads, linking or application runner are
wired into it. IDE appearance is user-verified.

Future builds belong to [b](https://github.com/vex-graph/b). No runnable editor
target or standalone runtime build is claimed by this metadata entry.

## Current State

**Draft — not finalized.** Every contract below is a design goal.

**Role:** R5 interactable — jGRASP/Zed-class mini IDE: shell-out toolchains,
xlsx viewer, grammar highlighting via `language`.

**Implemented and proven:** nothing. `LICENSE`, `CONTRIBUTING.md`, `README.md`,
`.gitignore` and an IDE-only `LANGUAGES NONE` `CMakeLists.txt` only; no tracked
`src/`, no editor code and no test partition (`src/oop/` and `src/cli/` are
reserved and empty).

**Platforms proven:** none.

## What it is
`semicolon` is the end-user code editor of the ecosystem: panes and editing
surfaces on `darling-framework`, syntax from `language` dylibs, builds driven
through `hotcwap` process supervision — supervised as an R1 `Application`.

## Depends on (Vertical Integration Law allowlist)
Borrows shapes from R1–R4 (arenas, windows, GPU, UI, grammars) to build;
owns no OS/window/memory management itself. Standalone-capable or
Kernel-registered.

## Scope and Limitations

**Scope (intended):** the end-user code editor — panes and editing surfaces on
`darling-framework`, syntax from `language` dylibs, builds driven through
`hotcwap` process supervision; supervised as an R1 `Application`.

**Deliberately not covered:** no OS/window/memory management (borrowed from
R1–R4); no grammar engine of its own (`language` owns grammars); no ecosystem
contract is owned here.

**Known limits and gaps:** this is an editor blueprint, not a finished IDE. R2 is
split between Vexspoke CPU computation/behavior and Relational Engine
memory/storage/native C search; migration is staged with the retained Vexspoke
ABI/default allocator. No C/Rust atomic-layout equivalence, automatic schema
migration or implemented app integration is implied. See the ecosystem readiness
Gist for granular scope and gaps.

## Layout
- `src/oop/` — editor object model (reserved).
- `src/cli/` — command-line entry surface (reserved).
- Tests: the shared `tests/` repo hosts a `tests/semicolon/` partition (mirrored
  per unit, the Test Tree Mirror Law); no test file lives inside this repo's
  source directories (the Test Segregation Law).

## Laws that govern work here
- Constitution: the [canonical preferences.md Gist](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a); real, Git-ignored workspace-root `../../../preferences.md`, not a Vexspoke file or symlink.
- Commits land in THIS repo root, one cohesive unit each; never push unless asked.
- One public class per `.h`/`.c` pair, `(*ptr).field` (never `->`), dest-last
  params, `-Wall -Wextra -Werror`.
