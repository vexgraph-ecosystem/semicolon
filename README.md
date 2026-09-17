# semicolon — R5 mini IDE

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

## Layout
- `src/oop/` — editor object model (reserved).
- `src/cli/` — command-line entry surface (reserved).
- Tests: umbrella `tests/` has no `semicolon/` partition yet; until then keep
  seam tests in-repo under `tests/` (never inside source dirs, per the Test
  Segregation Law).

## Laws that govern work here
- Constitution: `../../preferences.md` (umbrella symlink → `ecosystem/vexspoke/preferences.md`).
- Commits land in THIS repo root, one cohesive unit each; never push unless asked.
- One public class per `.h`/`.c` pair, `(*ptr).field` (never `->`), dest-last
  params, `-Wall -Wextra -Werror`.
