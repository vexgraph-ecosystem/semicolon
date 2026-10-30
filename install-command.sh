#!/bin/sh
# MODULE: explicit per-user command installation. Links the real shell launcher
# under semicolon and smc, plus the isolated preview under smc2, in a chosen user
# bin directory. Never overwrites another command. Preflight all names first;
# never edits shell configuration, builds, or launches the terminal.
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
for name in semicolon smc smc2; do
    dest=$bin/$name
    target=$root/semicolon
    if [ "$name" = smc2 ]; then
        target=$root/smc2
    fi
    if [ -e "$dest" ] || [ -L "$dest" ]; then
        if [ ! -L "$dest" ] || [ "$(readlink "$dest")" != "$target" ]; then
            printf 'Refusing to replace existing command: %s\n' "$dest" >&2
            exit 1
        fi
    fi
done
for name in semicolon smc smc2; do
    dest=$bin/$name
    target=$root/semicolon
    if [ "$name" = smc2 ]; then
        target=$root/smc2
    fi
    if [ -L "$dest" ]; then
        printf 'Already installed: %s\n' "$dest"
    else
        ln -s "$target" "$dest"
        printf 'Installed: %s\n' "$dest"
    fi
done
case ":$PATH:" in
    *":$bin:"*) printf '%s\n' 'Run smc, semicolon or smc2 from any directory.' ;;
    *) printf 'Add this directory to your shell PATH: %s\n' "$bin" ;;
esac
