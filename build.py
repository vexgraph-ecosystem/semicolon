#!/usr/bin/env python3
"""Explicit strict native build for the semicolon terminal shell.

MODULE: build.py
REGISTRY: sources(determinant, vexspoke, engine, language) lists the real toolkit,
consumer bridge, C23 lexical dependencies and borrowed R2 units. run(argv, timeout)
executes a bounded compiler child. build(output, compiler) resolves explicit
dependency roots, compiles those sources, and publishes the binary
by atomic replacement. No fetch, no installation, no program run, no tty use.
Standalone consumers supply source roots through the SEMICOLON_*_SOURCE_DIR
environment variables; the workspace paths are discovery defaults, not an
ecosystem dependency. A missing header is a real build failure.
SEMICOLON_LANGUAGE_SOURCE_DIR names the Language repository root; unlike the
other source-root options, that owner has no src/ subdirectory.
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


def c23_std_flag(compiler):
    """Pick the newest C23 spelling this compiler accepts, probing once.

    Apple clang before 16 accepts `-std=gnu2x` but rejects `-std=gnu23`; newer
    LLVM and GCC accept both. The probe is cold (one trivial syntax-only
    compile) and the chosen spelling is stated when it is the fallback.
    """
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as probe:
        probe.write("int main(void) { return nullptr != 0 ? 0 : 1; }\n")
        probe_path = probe.name
    try:
        for flag in ("-std=gnu23", "-std=gnu2x"):
            result = run([compiler, flag, "-fsyntax-only", probe_path], timeout=60)
            if result.returncode == 0:
                if flag != "-std=gnu23":
                    print(f"semicolon build: compiler rejects -std=gnu23; "
                          f"using {flag}", file=sys.stderr)
                return flag
    finally:
        Path(probe_path).unlink(missing_ok=True)
    raise SystemExit("semicolon build: compiler accepts neither -std=gnu23 "
                     "nor -std=gnu2x")


def sources(determinant, vexspoke, engine, language):
    produced = sorted(determinant.glob("terminal/*.c"))
    produced += [vexspoke / "primitive/string.c", vexspoke / "atomic/spin.c",
                  engine / "nio/mem.c"]
    produced += [ROOT / "src/editor/semicolon_syntax.c"]
    produced += [language / name for name in (
        "adapters/c/c23_highlights.c", "adapters/c/c23_lexer.c",
        "adapters/c/c23_lex_step.c", "syntax/role.c",
        "analysis/syntax_span.c", "document/source_range.c", "text/bounded_text.c")]
    return produced


def build(output, compiler="clang"):
    determinant = Path(os.environ.get("SEMICOLON_DETERMINANT_SOURCE_DIR",
                                      WORKSPACE / "suite/ecosystem/determinant/src"))
    vexspoke = Path(os.environ.get("SEMICOLON_VEXSPOKE_SOURCE_DIR",
                                   WORKSPACE / "suite/ecosystem/vexspoke/src"))
    engine = Path(os.environ.get("SEMICOLON_ENGINE_SOURCE_DIR",
                                  WORKSPACE / "suite/ecosystem/relational-engine/src"))
    language = Path(os.environ.get("SEMICOLON_LANGUAGE_SOURCE_DIR",
                                    WORKSPACE / "suite/ecosystem/language"))
    entry = ROOT / "src/main/tui-main.c"
    if not entry.is_file():
        raise ValueError(f"missing entry point: {entry}")
    flags = [c23_std_flag(compiler), "-Wall", "-Wextra", "-Werror", "-O2"]
    curses_library = "-lncurses"
    if sys.platform == "darwin":
        flags += ["-arch", "arm64", "-mcpu=apple-m1", "-mmacosx-version-min=14.0"]
    else:
        # Linux commonly splits the wide-character ABI into ncursesw; Darwin's
        # system ncurses exports the same wide API from -lncurses.
        curses_library = "-lncursesw"
    output = Path(output).absolute()
    if output.is_symlink() or output.is_dir():
        raise ValueError("invalid build output")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".semicolon-build-", dir=output.parent) as temp:
        artifact = Path(temp) / "semicolon-tui"
        argv = [compiler, *flags, "-I", str(ROOT), "-I", str(ROOT / "src"),
                "-I", str(determinant), "-I", str(vexspoke), "-I", str(engine), "-I", str(language),
                str(entry), *(str(source) for source in sources(determinant, vexspoke, engine, language)),
                 curses_library, "-lm", "-o", str(artifact)]
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
