# Contributing to semicolon

Read the complete [workspace constitution](https://gist.github.com/vex-graph/4132a6c45cb6d3797c3e8eff2e94035a)
first, then applicable test laws, current checklist and actual implementation.
Workspace law list: `../../metadata/gists/preferences/gist.md`; read relevant laws with `./tools/prefs`.
No repo-local lawbook exists here; this reading gap does not waive universal laws.

This R6 superapp is unfinished. Compose R1–R5 public contracts and registered
workflows; Primeagen remains independently buildable R5. Lower tiers never
include R6 code. Do not implement OS/window/memory ownership in the superapp.
See README.md for the staged Vexspoke/Relational Engine split. Tests belong in
the shared tests repository. Document actual proof and gaps; never promote a
blueprint from compilation alone. Commit locally under the Git Workflow Law;
never push without an explicit one-off instruction.

## Language consumer boundary

Language owns reusable document, project-analysis, adapter and index contracts;
Semicolon owns opening projects, discovering and presenting their files, and
the IDE workflow. The current Language engine accepts a caller-fed project
inventory and exposes an explicit `c23` lexer adapter. See the [Language
engine status and limitations](../../ecosystem/language/README.md) before
building a consumer: parsing, automatic file discovery and the Semicolon UI
integration are still in progress, and C99 or another dialect must not be
selected implicitly as C23.
