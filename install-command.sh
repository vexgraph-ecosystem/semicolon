#!/bin/sh
# MODULE: explicit per-user command installation. Links the real shell launcher
# into a chosen existing/new user bin directory. Never overwrites another command,
# edits shell configuration, builds, or launches the terminal.
# Usage: sh install-command.sh [bin-directory]; default $HOME/.local/bin.
# A successful link remains tied to this checkout, not a relocatable app bundle.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$#" -gt 1 ]; then
    printf '%s\n' 'Usage: sh install-command.sh [bin-directory]' >&2
    exit 1
fi
bin=${1:-"$HOME/.local/bin"}
mkdir -p "$bin"
bin=$(CDPATH= cd -- "$bin" && pwd)
dest=$bin/semicolon
if [ -e "$dest" ] || [ -L "$dest" ]; then
    if [ -L "$dest" ] && [ "$(readlink "$dest")" = "$root/semicolon" ]; then
        printf 'Already installed: %s\n' "$dest"
        exit 0
    fi
    printf 'Refusing to replace existing command: %s\n' "$dest" >&2
    exit 1
fi
ln -s "$root/semicolon" "$dest"
printf 'Installed: %s\n' "$dest"
case ":$PATH:" in
    *":$bin:"*) printf '%s\n' 'Run semicolon from any directory.' ;;
    *) printf 'Add this directory to your shell PATH: %s\n' "$bin" ;;
esac
