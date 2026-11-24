#!/usr/bin/env python3
"""Generate clangd commands from native Makefile dry runs and the local MinGW toolchain."""

import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
BUILDS = {
    "app/client/backend": ("debug", "regression-build", "lifecycle-test-build"),
    "app/firewall": ("debug", "test-build", "lifetime-test-build"),
    "app/notifications": ("debug", "test-build"),
}
COMPILERS = {"x86_64-w64-mingw32-gcc": "c", "x86_64-w64-mingw32-g++": "c++"}


def run(arguments, cwd=ROOT):
    return subprocess.run(
        arguments, cwd=cwd, input="", capture_output=True, text=True, check=True,
        env={**os.environ, "LC_ALL": "C", "MAKEFLAGS": "", "MFLAGS": ""},
    )


def toolchain_flags(compiler, language):
    """Discover system headers without requiring editor-specific --query-driver options."""
    target = run([compiler, "-dumpmachine"]).stdout.strip()
    builtin = Path(run([compiler, "-print-file-name=include"]).stdout.strip()).resolve()
    output = run([compiler, "-E", "-x", language, "-", "-v"]).stderr
    flags = [f"--target={target}"]
    collecting = False
    for line in output.splitlines():
        if line == "#include <...> search starts here:":
            collecting = True
        elif line == "End of search list.":
            break
        elif collecting:
            path = Path(line.strip()).resolve()
            # Clang must use its own intrinsic headers, not GCC's implementation.
            if path != builtin and path != builtin.with_name("include-fixed"):
                flags.extend(["-isystem", str(path)])

    if len(flags) == 1:
        raise ValueError(f"No system include paths found for {compiler}")
    return flags


def parse_commands(output, directory):
    """Split compile-and-link recipes into one database entry per source file."""
    for line in output.splitlines():
        if not line.startswith(tuple(COMPILERS)):
            continue
        arguments = shlex.split(line)
        if arguments[0] not in COMPILERS:
            continue

        sources = []
        flags = []
        tokens = iter(arguments[1:])
        for token in tokens:
            if token in ("-o", "-MF", "-MT", "-MQ"):
                next(tokens)
            elif token in ("-c", "-MMD", "-MP"):
                continue
            elif not token.startswith("-") and Path(token).suffix in (".c", ".cpp"):
                sources.append((directory / token).resolve())
            elif not token.startswith("-") and Path(token).suffix in (".o", ".a"):
                continue
            elif token.startswith(("-l", "-L", "-Wl,", "-static")):
                continue
            else:
                flags.append(token)

        for source in sources:
            yield {
                "directory": str(directory),
                "file": str(source),
                "arguments": [arguments[0], *flags, "-c", str(source)],
            }


def generate(root=ROOT):
    entries = {}
    toolchains = {}
    for module, targets in BUILDS.items():
        directory = root / module
        # Each module is visited separately. Disable recursive make recipes in dry runs.
        output = run([
            "make", "--dry-run", "--always-make", "--no-print-directory", "--jobs=1",
            "MAKE=true", "BUILD_MODE=debug", *targets,
        ], cwd=directory).stdout

        commands = list(parse_commands(output, directory))
        if not commands:
            raise ValueError(f"No compilation commands found in {module}")
        for entry in commands:
            if Path(entry["file"]).is_relative_to(root / "lib/mbedtls"):
                continue
            if entry["file"] in entries:
                continue
            arguments = entry["arguments"]
            name = arguments[0]
            if name not in toolchains:
                compiler = shutil.which(name)
                if not compiler:
                    raise ValueError(f"Required compiler is not on PATH: {name}")
                toolchains[name] = (compiler, toolchain_flags(compiler, COMPILERS[name]))
            compiler, flags = toolchains[name]
            entry["arguments"] = [compiler, *flags, *arguments[1:]]
            entries[entry["file"]] = entry

    return [entries[source] for source in sorted(entries)]


if __name__ == "__main__":
    try:
        commands = generate()
        destination = ROOT / "build/compile_commands.json"
        destination.parent.mkdir(exist_ok=True)
        destination.write_text(json.dumps(commands, indent=2) + "\n", encoding="utf-8")
        print(f"Wrote {len(commands)} compilation commands to {destination}")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(error.stderr if isinstance(error, subprocess.CalledProcessError) else str(error))
