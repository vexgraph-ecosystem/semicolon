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
shows its files, and can display a selected file in an editor. Markdown files
open in a rendered preview with a source view available. Code view supports
basic text editing and attempts to save after one idle second; failed saves keep
the edited buffer available and show an error state.

The GUI is also planned, but it is a substantial longer-term effort: it needs to
communicate and compose with Darling rather than grow a competing widget and
windowing stack inside Semicolon. Language support, explicit bounded build-tool
use through `b`, and a future optional Primeagen agent workflow are planned too.
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

## Scope and Limitations

The current shell is an initial file browser and editor, not a complete IDE or
the finished TUI. The GUI and its Darling integration are not implemented.
Editing is intentionally basic: there is no selection, undo history, mouse
caret placement, or syntax highlighting. Build integration, language support,
agent workflows, and the broader multi-window application are not implemented.
Markdown rendering is a bounded terminal-oriented subset, not a browser or full
CommonMark implementation.

Saving uses staged replacement and ordinary file-conflict checks. It is not a
crash-durable journal or a hostile-writer sandbox. A failed save preserves edits
while the process remains alive; there is no recovery journal after termination.
The only platform with current executed shell evidence is macOS arm64.

## Running

Run `./semicolon` from a project directory to open that project, or pass
`--root PATH` to select one explicitly. `--file PATH` opens a particular file;
`--dump WxH` prints a frame for non-interactive inspection. The launcher builds
the terminal shell on first use. It does not download dependencies or run the
project being viewed.

## Layout

- `src/main/tui-main.c` — current terminal shell and basic editor.
- `src/tui/tui_example.txt` — the hand-made terminal design and its notes.
- `semicolon`, `build.py`, `install-command.sh` — launcher, explicit build, and
  optional per-user command link.
- Shared tests are in `../../tests/semicolon/`.
