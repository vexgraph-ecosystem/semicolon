#!/usr/bin/env python3
"""Explicit strict native build for the semicolon terminal shell.

MODULE: build.py
REGISTRY: build(output, compiler) resolves explicit dependency roots, compiles the
real Determinant toolkit plus the R2 sources it borrows, and publishes the binary
by atomic replacement. No fetch, no installation, no program run, no tty use.
Standalone consumers supply source roots through the SEMICOLON_*_SOURCE_DIR
environment variables; the workspace paths are discovery defaults, not an
ecosystem dependency. A missing header is a real build failure.
The compiler and the output parent are trusted; a failed compile preserves any
existing output and reclaims its own temporary directory.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WORKSPACE = ROOT.parents[2]


def run(argv, timeout=180):
    return subprocess.run(argv, capture_output=True, text=True, timeout=timeout)


def sources(determinant, vexspoke, engine):
    produced = sorted(determinant.glob("terminal/*.c"))
    produced += [vexspoke / "primitive/string.c", vexspoke / "atomic/spin.c",
                 engine / "nio/mem.c"]
    return produced


def build(output, compiler="clang"):
    determinant = Path(os.environ.get("SEMICOLON_DETERMINANT_SOURCE_DIR",
                                      WORKSPACE / "suite/ecosystem/determinant/src"))
    vexspoke = Path(os.environ.get("SEMICOLON_VEXSPOKE_SOURCE_DIR",
                                   WORKSPACE / "suite/ecosystem/vexspoke/src"))
    engine = Path(os.environ.get("SEMICOLON_ENGINE_SOURCE_DIR",
                                 WORKSPACE / "suite/ecosystem/relational-engine/src"))
    entry = ROOT / "src/main/tui-main.c"
    if not entry.is_file():
        raise ValueError(f"missing entry point: {entry}")
    flags = ["-std=gnu23", "-Wall", "-Wextra", "-Werror", "-O2"]
    if sys.platform == "darwin":
        flags += ["-arch", "arm64", "-mcpu=apple-m1", "-mmacosx-version-min=14.0"]
    output = Path(output).absolute()
    if output.is_symlink() or output.is_dir():
        raise ValueError("invalid build output")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".semicolon-build-", dir=output.parent) as temp:
        artifact = Path(temp) / "semicolon-tui"
        argv = [compiler, *flags, "-I", str(determinant), "-I", str(vexspoke), "-I", str(engine),
                str(entry), *(str(source) for source in sources(determinant, vexspoke, engine)),
                "-lncurses", "-lm", "-o", str(artifact)]
        result = run(argv)
        if result.returncode:
            raise ValueError(result.stderr.strip() or "compile failed")
        os.replace(artifact, output)
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / ".build/semicolon-tui")
    parser.add_argument("--compiler", default="clang")
    args = parser.parse_args()
    try:
        print(build(args.output, args.compiler))
    except (OSError, ValueError) as error:
        print(f"[vex] semicolon build: {error}", file=sys.stderr)
        raise SystemExit(1)
