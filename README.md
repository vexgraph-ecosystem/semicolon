# semicolon — R5 mini IDE

## CLion: CMake is IDE metadata only

Open this repository root as a CMake project. `CMakeLists.txt` is an IDE-only
blueprint entry: there are no production sources or C23 source targets yet,
so there is nothing to provide semantic diagnostics or inlay hints for.
No fake declarations, dependency downloads, linking or application runner are
wired into it. IDE appearance is user-verified.

Future builds belong to [b](https://github.com/vex-graph/b). No runnable editor
target or standalone runtime build is claimed by this metadata entry.

**Role:** R5 Interactable — jGRASP/Zed-class mini IDE: shell-out toolchains,
xlsx viewer, grammar highlighting via `language`.
**Status:** early shell (`src/oop/`, `src/cli/` reserved, empty).

## What it is
`semicolon` is the end-user code editor of the ecosystem: panes and editing
surfaces on `darling-framework`, syntax from `language` dylibs, builds driven
through `hotcwap` process supervision — supervised as an R1 `Application`.

## Depends on (Vertical Integration Law allowlist)
Borrows shapes from R1–R4 (arenas, windows, GPU, UI, grammars) to build;
owns no OS/window/memory management itself. Standalone-capable or
Kernel-registered.

**Unfinished application:** this is an editor blueprint, not a finished IDE.
R2 comprises Vexspoke CPU computation/behavior and Relational Engine
memory/storage, stable rows, bindings and native C search. Migration is staged;
existing Vexspoke memory/container ABI and default allocator remain. R1 owns
lifetimes/residency; GPU shaders/dispatch remain Graphvex R3. No C/Rust atomic
layout equivalence, automatic schema migration or implemented app integration is
implied. See the ecosystem readiness wiki for granular scope and gaps.

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
