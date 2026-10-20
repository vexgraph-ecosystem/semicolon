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
panels and texts, over a **real file** in a **real project**. The project is the
directory it was started in (`--root PATH` overrides), so `cd <project> && semicolon`
shows that project; the **rail is its file tree**, read one directory at a time and
folded by clicking; a click on a file opens it in the showable editor with live line
numbers. `--file PATH` opens a specific file, and a per-project **session** (the open
file, the divider) is remembered outside the project so the next run resumes.

**A present is a transaction.** Determinant brackets every frame that carries a
change — the bracket opens only when there is something to send and closes even if a
sink refused part-way, so the device is never left inside a half-finished frame. The
shell maps that onto DEC private mode **2026** (synchronized output), which asks the
terminal to apply the update as one step, so a divider drag or a resize is never
caught half-drawn. Terminals that don't know the mode ignore it — and **Terminal.app
is one of them**, so this is invisible there; kitty, WezTerm, iTerm2, Alacritty and
Ghostty honour it.

Output is quit with **Ctrl-C** only — there is deliberately no letter shortcut,
because a keystroke is input and `q` is a legitimate thing to want to type.

The layout is now borderless: the header carries `[(); Code]` and `[>w< agents]`
on the left and the tab strip starts at the rail seam. The seam/divider is invisible
at rest and appears only while the pointer hovers its drag target. The footer keeps the file
breadcrumb and right-side status chips without a dashed rule. Disclosure shapes
remain Unicode from the canvas through ncurses' wide-character API; the host selects
the environment's character locale before initializing curses. The macOS system
ncurses wide API links through `-lncurses`; Linux uses `-lncursesw`. Editor rows use
two leading spaces, a right-aligned line number and period, then two spaces before code.

Editor line numbers are right-aligned to the width of the file's largest line
number, with a period directly after that field and two cells before the code.
The width is derived from the file, not fixed to a three- or four-digit guess.

Scrolling is **two-axis**, and both axes scroll *past* the content: 10 empty lines
follow the last line and 20 empty columns follow the longest one, so there is always
somewhere to type at the bottom and at the right edge instead of the caret being
pinned against the frame. Scrolling is **one line per wheel notch** and carries **momentum**: a burst decelerates
and settles by itself rather than running at a constant speed until it empties, and a
**press catches it** — touching anywhere stops the motion, so having momentum is safe.
It runs at about **60fps** while in flight and sleeps the long poll once it settles; an
idle shell still emits nothing, because a frame that changes nothing opens no
transaction. Friction is two named constants (`TUI_SCROLL_EASE`, `TUI_SCROLL_EPSILON`),
so how much glide it has is a tuning decision rather than a rewrite.

Arrows are the primary input: **←/→ pan, ↑/↓ scroll, PageUp/PageDown page, Home/End
jump** (both the CSI and SS3 cursor-key forms are bound, because terminfo entries
often list only one). A wheel-right pans, Shift+wheel pans, and a drag at the code
area's first or last column pans — but those are conveniences: a horizontal wheel
is a pair most terminals never send and Shift+wheel is routinely eaten by the
terminal's own scrollback, so the keys are the path that always exists.

**The tracking line** is the answer for a terminal that never reports horizontal
scrolling — macOS Terminal.app, for one. The bottom TWO rows are one horizontal
slider with no drawn chrome — the last editor row and the footer,
so the whole floor of the window is grabbable. A drag maps the pointer straight onto
the offset, so dragging to an end reaches that end of the line. A *click* there does
nothing, because press and drag are separable gestures, so passing the pointer
across the border never disturbs the view. It needs only drag events, which every
terminal sends. The vertical edge zone sits two rows above it.

The rail divider is **draggable**.

### Icon setting

Nerd Font icons are the default: distinct open/closed folders and filename-based
file icons in the rail and tabs. Exact build names (Makefile, CMakeLists.txt) take
precedence over case-insensitive extensions; unknown files get a generic icon.
C and header files have distinct badges. This is presentation, not language detection.

Select a Nerd Font in your terminal's font preferences. For an unpatched font,
use `semicolon --icons plain` or persist `SEMICOLON_ICONS=plain` in your shell
environment. Plain mode keeps Unicode disclosures and uses `[]` folders and `-`
files, with no private-use icons. `--icons nerd` selects icons explicitly and
overrides a valid environment setting. Unknown modes reject with a diagnostic.
There is no settings screen or automatic font-coverage detection yet; terminal
glyph appearance/width remains user acceptance. Icons do not replace `!!` warnings.

Colour carries **real alpha**, which is what makes a fade possible. A terminal has no
alpha channel — `SGR` sets *one* colour — so a translucent pen is composited by the
shell before the device sees it: the background plane over the terminal's own
background, and then the ink over that. Two consequences worth knowing:

- **One colour, any theme.** The gutter is a single translucent value, so it recedes on
  a dark theme *and* on a light one — it lands around `#5F5F6B` on black and `#D6D6DC`
  on white. Fully opaque artwork is passed through untouched, so nothing that was
  already opaque changed.
- **The shell must still be told the theme**, because a fade has to be composited
  *against* something. It asks the terminal (OSC 11) and falls back if there is no
  answer; `SEMICOLON_BG=#RRGGBB` overrides it, and `semicolon --bg-probe` prints what it
  found and where it came from.

The design's one **highlight** is the open tab, and it is a tint rather than a block —
22% of the accent over whatever the theme paints, so it reads as "this one" without
covering the design up. Everything else stays transparent: a full repaint emits exactly
one background colour across the whole frame.

Scrolling is free because text is addressable: the editor's rows are created once
and re-pointed with `TerminalText_setText`, so no row is allocated, freed or
rebuilt while scrolling. The divider is dragged by moving an **anchor** — the same
layer gets a new extent through `TerminalPanel_setAnchor` — so resizing a pane and
scrolling a file are both ordinary state changes, not scene surgery.

The **tab strip** is still literals, nothing is edited, and there is no syntax
highlighting. `--dump WxH` prints one frame as plain text, which is how the owner
checks the layout without a terminal. `tests/semicolon/tui_main_test.py` asserts the
borderless geometry (rail seam at column 30), dynamic number alignment including a
five-digit line count, the non-tty refusal, and — under a
real pty on a scratch project — that mouse reporting is enabled and switched off
again, that a wheel over the rail scrolls the rail and not the editor, that clicking
a directory opens and closes it, that clicking a file opens it (and the header and
tab follow), that a divider drag survives, and that the session round-trips. It also
proves the colour path: the pty plays the terminal and answers the OSC 11 background
query, and a translucent highlight is asserted to reach the device as the accent
composited over that backdrop.

Also present: `build.py` (explicit, strict, no fetch and no program run), the
`semicolon` launcher, and `install-command.sh` for a per-user command link.

The rail seam is drawn by the shell with the canvas primitive, because the toolkit
has no vertical-rule widget yet. It stays invisible until hovered. There are no
decorative outer edges.

`semicolon --input-probe` is a diagnostic: it prints the mouse and key events the
terminal actually sends, so whether a trackpad reports a horizontal wheel (buttons
66/67) is a measurement rather than a guess.

`tests/semicolon/tui_main_test.py` asserts the borderless geometry, dynamic number
alignment, non-tty refusal, and — under a real pty — that mouse reporting is enabled
and switched off again, wheel events scroll, and divider and horizontal tracking-line
drags survive. It asserts the frame's *shape* rather than comparing rows against
`src/tui/tui_example.txt`, because that file is a notebook as much as a design:
rows 1 and 13 there carry trailing annotations past the frame, so no row of it is
a 112-cell row any more.

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

**The terminal shell is a viewer, not an editor.** One real file is read and
displayed; nothing is written, no tab is switched, no key edits a buffer, and
there is no syntax highlighting, no build integration and no agent. There is no
caret: the scroll-past padding reserves the space a caret would need, but nothing
inserts yet. Resize
re-lays the whole tree because every rectangle is recomputed from the canvas on
each paint. There is no hover and no focus, and the only click is the rail's file
tree: Determinant decodes the
mouse and can resolve a cell to the child under it; the host routes rail clicks,
wheel scrolling, edge autoscroll and divider drag, but not editor hover/focus.
The chip bars are texts rather than the segment
widget they will become, and vertical rules are drawn by this shell instead of by
a toolkit widget.

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
  mouse byte feed, keys, resize, file read) plus the scene, built on Determinant's
  panels, texts, anchors and mouse decoder.
- `src/tui/tui_example.txt` — the design the shell follows, and the author's notes,
  glyph vocabulary and reference palette. Not machine-parsed: its rows carry
  annotations.
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
