# semicolon — R6 IDE superapp

Semicolon is meant to be a lightweight integrated development environment: one
place to see a project, edit its files, and bring useful development workflows
together without turning the environment into a pile of unrelated features.
It is not trying to become a new Vim. The goal is an approachable, integrated
workspace that stays small and lets the ecosystem's focused tools do their jobs.
The intended product has both a terminal UI (TUI) and a graphical UI (GUI).

## Current State

Semicolon is an early R6 draft, not a finished IDE. Its runnable piece today is
a terminal shell launched with `./semicolon`. It opens a project directory,
shows its files, and can display a selected file in an editor. Open files have
switchable tabs; tabs close with their Nerd-font X or middle-click, and clicking
the source view places the caret. Markdown files
open in a rendered preview with a source view available. Code view supports
basic text editing and attempts to save after one idle second; failed saves keep
the edited buffer available and show an error state.

C23 lexical coloring now consumes Language's conservative `C23Highlights` pass
through `src/editor/semicolon_syntax.{h,c}`. The initial host policy selects C23
for lowercase `.c` and `.h` files, not C++ or Objective-C. Keywords are dark
green; user types are teal; functions are blue; variables, parameters and members
are cyan; macros/constants are pale yellow and bold; preprocessor markers and
directive names such as `#include` and `#define` share that macro pen; strings,
characters and numbers are light green; regular comments are gray and
documentation comments are muted green; brackets are bold white. These are
screenshot-inspired defaults, not an exact imported theme. Function/type/variable/macro roles come from local
spelling and neighboring-token heuristics, not a parser or symbol resolver, so a
capitalized variable can read as a type and an all-caps local as a macro. The
bridge's role pens have a validated configuration API, but no theme settings
screen or persistent syntax palette exists yet. Each tab owns a copied complete
analysis snapshot; edits request cold refresh, while scrolling and caret movement
reuse it. A failed refresh keeps editing available without stale highlighting.
Coloring changes ink/weight only, never source, saved bytes or caret positions.
Italic documentation comments, parameter inlays, diagnostics, completion and
cross-file semantics are not wired. The live presenter currently maps RGB to the
terminal palette, not exact truecolor. The host terminal owns its font and
ligature features; Semicolon sets neither. `python3 tests/semicolon/editor/run.py`
owns strict C23, ASan/UBSan, injected allocation-failure recovery and
editor-cell/PTY proof; appearance remains yours.

The tree shows `.gitignore`, `.gitkeep`, and the `.github` directory while
keeping other dot entries such as `.idea` and `.vscode` hidden. It reads the
project-root `.gitignore` as text and colors matching visible tree entries orange;
this is independent of Git status and refreshes when the ignore file changes.
Patterns support comments, `!` negation, `*`/`?`/bracket globs, path components,
and trailing-slash directory rules; full Git ignore semantics (including escaped
trailing spaces and nested ignore files) are not claimed.

The project folder stays pinned at the top of the file rail beside its grayscale
canonical location (`~/...` inside HOME, otherwise an absolute path). Long names
and paths show an ellipsis within the rail; children scroll below without moving
the header or the editor's file tabs. Each tree level advances by two spaces;
deep indentation is capped so filenames remain readable.

Scrolling follows the pointer: over the file tabs, either wheel axis pans the
strip by whole tabs; over the tree, up/down scrolls children and left/right pans
long names. Modified vertical wheel events also pan the tree sideways. Over the
source editor, the existing vertical/horizontal scrolling remains independent.
Newly selected tabs reveal themselves; clipped titles retain a close control.
These routes have scripted PTY evidence, not physical Ghostty or font approval.

The file tree opens at 50 terminal cells by default. The visible filename space
is smaller than that total because the tree also uses cells for indentation,
disclosure markers and icons. Dragging its divider to
resize records that custom width; collapsing the tree leaves a persistent
three-line grip at the far left, and clicking it reopens the tree at the
remembered custom width or the 50-cell default. Older session files that only
stored the former default width no longer force the tree to reopen narrow.

The hover divider grip sits one cell beyond the file list's last cell, with its
mouse target at the same position, so it does not cover list text or selection.

The code view shows Run only for supported file formats; `.c` and `.m` files
must contain a text-detected `main` definition. C files under this checkout's
`tests/` tree route to its `tools/b run <absolute-source>` graph (registered
targets or that graph's on-demand source resolution), not the standalone
single-file compiler. External C/Objective-C files still use standalone
`b run exec`; supported scripts/source extensions use standalone `b run instance`.
Detection does not prove a file builds or that its dependencies are known; no
general project-graph discovery is offered. Starting a file opens a separate
selectable console panel, not an overlay on source text or its line-number gutter.
The rail remains available; only the selected file or console tab is highlighted.
The console retains the latest 10,000 lines in memory, at most 2,048 bytes per
line; its header counts evicted lines and clipped long lines. Wheel, arrows,
Page Up/Down, Home and End browse output; new data follows the tail until the
user scrolls up. Completion shows the actual child result (`exit 0`, another exit
code, a terminating signal, or an unavailable status) in the tab and console
heading; it never infers success from a quiet child. For `b run` of a windowed
application, that result describes `b`'s process, not proof the window rendered.
Finishing leaves the output intact and scrollable until its tab closes, while
the editor buffer stays untouched. Close its tab with the close
mark or middle-click to stop a still-running child.
Child stdin is currently `/dev/null`, so this is an initial run/observe workflow
rather than an interactive shell.

Markdown stays inside the terminal shell. Its Code and Preview controls show an
icon and label, and the active mode and selected project-tree row share one
accent color. Set `SEMICOLON_ACCENT=#RRGGBB` or pass `--accent #RRGGBB` to
change the accent; an interactive run saves the chosen color with that project's
session settings. Nerd Font glyph appearance depends on the terminal font.

Run `./semicolon icons` for the supported filename icon catalog. It lists the
mapped extension families, Nerd Font glyphs and their RGB colors using ANSI
true-color output. The catalog is broad, not universal: unlisted extensions use
the generic file icon, and glyph rendering depends on the terminal font.

The GUI is also planned, but it is a substantial longer-term effort: it needs to
communicate and compose with Darling rather than grow a competing widget and
windowing stack inside Semicolon. Broader Language services and a future optional
Primeagen agent workflow are planned too.
Those integrations are not implemented just because Semicolon is intended to
compose them. Primeagen remains a standalone R5 application and does not depend
on Semicolon.

The terminal shell has an executed macOS arm64 PTY owner, run against the
canonical workspace sources. The Markdown renderer has executed strict C23 and
ASan/UBSan owner coverage. Other platforms and physical terminal appearance
remain unproved.

## Why it exists

Development work is already spread across files, tools, and focused applications.
Semicolon is the place intended to connect those activities into a coherent
project workflow, while keeping implementation ownership with the tools that
already provide it. Its value should come from useful integration, not from
reimplementing every editor, language server, build system, or agent inside one
large application.

The R6 role is composition over R1–R5 public contracts. Lower-tier projects
retain their own lifetimes, implementations, and standalone use. Semicolon owns
the experience of bringing those workflows together, not their underlying
drivers or engines.

## Vertically integrated across vexgraph's stack

Semicolon is also the **dogfooding shell** for this workspace: open real vexgraph
source with `smc`, edit it, and invoke supported Run targets without pretending
that one source file knows its own include paths or link dependencies. `b` is
the standalone **orchestration layer**, not an R tier: the workspace's
`tools/b` runs its `tools/workspace.c` graph over independently owned libraries,
tests and applications. For C files under the workspace `tests/` tree, Semicolon
passes the absolute source to that graph; elsewhere it uses the generic
standalone `b` route. It does not embed either builder or synthesize a JSON
project manifest.

What crosses the levels *today* is narrower than the final R6 design:

| Owner | Working Semicolon contact | Not yet integrated |
| :--- | :--- | :--- |
| Relational Engine + Vexspoke (R2) | The shell build links engine native Memory and Vexspoke CPU primitives. | Live storage residency or schema migration through Hotcwap. |
| Determinant (R3) | Terminal panels, cells, caller-fed input and Markdown presentation. | Complete editor language services or physical-terminal visual approval. |
| Language (R3) | Its public C23Lexer is linked into SemicolonSyntax for live `.c`/`.h` lexical ink and bold brackets; host-owned row snapshots preserve multiline context. | Generic adapter registry/project/index integration, semantic classification, parameter inlays, parser and LSP remain unwired. |
| Hotcwap (R1), Graphvex (R3), Darling/Harness/Sesh (R4) | The workspace `b` graph may build their owners and tests, but invoking `b` does not link them into this TUI. | Supervised reload, GPU GUI, widgets, agent and session workflows. |
| Primeagen (R5) | Independent standalone application. | Optional registered workflow; no reverse dependency. |
| Semicolon (R6) | File browser/editor, separate bounded run console and user-triggered `b` child invocation. | Full superapp composition and GUI. |

The [workspace README](../../../README.md) documents the build graph and its
proof boundary. Browsing a repo, seeing `b` output, or receiving `exit 0`
does not prove a windowed app's behavior or a working Language integration.

## Running

Run `./semicolon` from a project directory to open that project, or pass
`--root PATH` to select one explicitly. `--file PATH` opens a particular file;
`--dump WxH` prints a frame for non-interactive inspection. The launcher builds
the terminal shell on first use. It does not download dependencies or run the
project being viewed.

For a short command, run `sh install-command.sh` once from this checkout. It
links both `smc` and `semicolon` to the same launcher in `~/.local/bin`; run
`smc` from any project directory if that bin directory is on `PATH`. Pass a bin
directory to the installer to choose another location. It never overwrites an
unrelated command, edits shell configuration, builds, or launches the UI.

## Layout

- `src/main/tui-main.c` — current terminal shell and basic editor.
- `src/editor/semicolon_syntax.{h,c}` — copied C23 lexical snapshot and configurable role-to-pen consumer bridge.
- `src/tui/tui_example.txt` — the hand-made terminal design and its notes.
- `semicolon`, `build.py`, `install-command.sh` — launcher, explicit build, and
  optional per-user `semicolon`/`smc` command links.
- Shared tests are in `../../tests/semicolon/`.

## Scope and Limitations

The current shell is an initial file browser and editor, not a complete IDE or
the finished TUI. The GUI and its Darling integration are not implemented.
Editing is intentionally basic: there is no selection or undo history. Syntax
coloring is the documented C23 lexical subset, not full semantic highlighting.
Whole-document refresh is synchronous cold work, not incremental analysis or a
large-project latency guarantee; malformed/unsupported lexical spans remain plain.
Standalone builds may set `SEMICOLON_LANGUAGE_SOURCE_DIR` to the Language repository
root (the directory containing `adapters/`, not a nonexistent `src/`). The launcher
watches that dependency's sources and headers for rebuilding, with no downloads.
General build integration beyond the workspace C-test route,
broader language services, agent workflows, and
the broader multi-window application are not implemented. The `b` run console
supports bounded scrollback and cancellation, but no child stdin, terminal
emulation, background-job management, or cross-platform PTY proof.
Markdown rendering is a bounded terminal-oriented subset, not a browser or full
CommonMark implementation.

Saving uses staged replacement and ordinary file-conflict checks. It is not a
crash-durable journal or a hostile-writer sandbox. A failed save preserves edits
while the process remains alive; there is no recovery journal after termination.
The only platform with current executed shell evidence is macOS arm64.
