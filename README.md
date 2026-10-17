# semicolon — R6 IDE superapp

Future ecosystem builds belong to [b](https://github.com/vex-graph/b). The one
runnable artifact today is the terminal shell, and it owns its own explicit build
path the way Primeagen does: `./semicolon` builds on first use and draws this
repository's own design from `src/tui/tui_example.txt` on the current terminal.

## Current State

**Draft — not finalized.** Every contract below is a design goal, except the
terminal shell, which runs.

**Role:** R6 IDE superapp: Language support, bounded `b` tool integration and
future Primeagen agent-client composition over R1–R5 public contracts.
Primeagen remains independently buildable R5, never dependent on Semicolon.

**Implemented and proven:** a **terminal shell** (`src/main/tui-main.c`) that
draws the hand-made design on a real terminal, on Determinant's component tree of
panels and texts. It is a fixed mock-up, not a working editor: the file tree, the
tab strip and the single code line are literals, nothing is read from disk and
nothing is edited. The window chrome (the rail verticals and the right edge) is
drawn by the shell with the canvas primitive, because the toolkit has no vertical
rule widget yet.

`--dump WxH` prints one frame as plain text, which is how the owner checks the
layout with no terminal involved. `tests/semicolon/tui_main_test.py` compares the
rendered title and rail rows against `src/tui/tui_example.txt` byte for byte,
checks the status row's structure, proves a non-tty launch is refused with a
diagnostic instead of a corrupt screen, and launches the shell under a real pty
to confirm it paints and leaves on `q`.

Also present: `build.py` (explicit, strict, no fetch and no program run), the
`semicolon` launcher, and `install-command.sh` for a per-user command link.

**Platforms proven:** macOS arm64 only, from the owner's pty run. The shell is
plain POSIX plus ncurses, but it has not been run anywhere else.

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

**The terminal shell is a mock-up, and that is all it claims.** It draws a fixed
scene: no file is opened, no tab is switched, no key edits anything, and there is
no syntax highlighting, no build integration and no agent. Resizing re-lays the
whole tree because every rectangle is recomputed from the canvas on each paint.
There is no hover, no click and no focus: the pointer primitives exist in
Determinant but nothing routes events through them yet, and the chip bars are
texts rather than the segment widget they will become. Vertical rules are drawn
by this shell with the canvas primitive instead of by a toolkit widget.

**Known limits and gaps:** this is an unfinished superapp/editor blueprint, not a finished IDE. No local
lawbook (`semicolon-preferences.md`) exists yet; that missing repo-local preferences file is an explicit
gap until this repository gains source, and the universal constitution plus applicable test laws still
bind (the Per-Repo Preferences Extension Law). R2 is
split between Vexspoke CPU computation/behavior and Relational Engine
memory/storage/native C search; migration is staged with the retained Vexspoke
ABI/default allocator. No C/Rust atomic-layout equivalence, automatic schema
migration or implemented app integration is implied. See the ecosystem readiness
Gist for granular scope and gaps.

## Layout
- `src/oop` — editor object model (reserved).
- `src/cli` — command-line entry surface (reserved).
- `src/main/tui-main.c` — the terminal shell: the host (tty, alternate screen,
  keys, resize) plus the scene, built on Determinant's panels, texts and anchors.
- `src/tui/tui_example.txt` — the design the shell reproduces.
- `semicolon`, `build.py`, `install-command.sh` — launcher, explicit build and
  per-user command installation.
- Tests live in the shared `../../tests` repo at `tests/semicolon/`, mirrored per
  unit (the Test Tree Mirror Law); no test file lives inside this repo's source
  directories (the Test Segregation Law).

## Laws that govern work here
- Constitution: the [canonical preferences.md Gist](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a); real, Git-ignored `../../gist/PREFERENCES.md`, not a Vexspoke file or symlink. Lower tiers never include R6 headers or code.
- Commits land in THIS repo root, one cohesive unit each; never push unless asked.
- One public class per `.h`/`.c` pair, `(*ptr).field` (never `->`), operations
  return their value (the Value Return Law — never a caller-provided
  destination), `-Wall -Wextra -Werror`.
