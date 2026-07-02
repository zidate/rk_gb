#!/usr/bin/env python3
"""Regression test for project compiler flags.

The historical lowercase -o3 token is not a GCC optimization option. GCC
parses it as "-o 3", so CMake's later "-o <object>" output flag makes the
compile fail with "cc1: error: too many filenames given".

Uppercase -O3 is intentionally rejected for the RK IPC application/middleware
targets because field reports showed encoder startup crashes from -O3 builds.
"""

from __future__ import annotations

import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_DIR = Path(__file__).resolve().parents[2]
TOOLCHAIN_PREFIX = os.environ.get("TOOLCHAIN_PREFIX", "arm-rockchip830-linux-uclibcgnueabihf-")


def discover_cmake() -> str:
    if os.environ.get("CMAKE_BIN"):
        return os.environ["CMAKE_BIN"]

    tools_dir = REPO_DIR.parent / ".tools"
    for candidate in sorted(tools_dir.glob("cmake-*/bin/cmake")):
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)

    cmake = shutil.which("cmake")
    if cmake:
        return cmake

    raise RuntimeError("cmake not found")


def discover_toolchain_bin() -> Path:
    if os.environ.get("RK_TOOLCHAIN_BIN"):
        return Path(os.environ["RK_TOOLCHAIN_BIN"])

    candidate = REPO_DIR.parent / "RK" / "arm-rockchip830-linux-uclibcgnueabihf" / "bin"
    if candidate.is_dir():
        return candidate

    raise RuntimeError("RK_TOOLCHAIN_BIN is not configured")


def configure_project(source_dir: Path, build_dir: Path, extra_options: list[str]) -> None:
    cmake = discover_cmake()
    toolchain_bin = discover_toolchain_bin()
    env = os.environ.copy()
    env["PATH"] = f"{toolchain_bin}:{env['PATH']}"
    env["CC"] = f"{TOOLCHAIN_PREFIX}gcc"
    env["CXX"] = f"{TOOLCHAIN_PREFIX}g++"

    cmd = [
        cmake,
        "-S",
        str(source_dir),
        "-B",
        str(build_dir),
        "-DCMAKE_POLICY_VERSION_MINIMUM=3.5",
        f"-DCMAKE_C_COMPILER={TOOLCHAIN_PREFIX}gcc",
        f"-DCMAKE_CXX_COMPILER={TOOLCHAIN_PREFIX}g++",
    ]
    cmd.extend(extra_options)
    subprocess.run(cmd, check=True, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def collect_compile_flag_tokens(build_dir: Path) -> dict[Path, list[str]]:
    tokens_by_file: dict[Path, list[str]] = {}
    for flags_make in build_dir.glob("**/flags.make"):
        tokens: list[str] = []
        for line in flags_make.read_text().splitlines():
            if line.startswith(("C_FLAGS", "CXX_FLAGS")):
                _, flags = line.split("=", 1)
                tokens.extend(shlex.split(flags))
        if tokens:
            tokens_by_file[flags_make] = tokens
    return tokens_by_file


def main() -> int:
    projects = [
        (
            "middleware",
            REPO_DIR / "Middleware",
            ["-DRV1106_DUAL_IPC=ON", "-DRC0240_LGV10=ON", "-Drelease=ON"],
        ),
        (
            "app",
            REPO_DIR,
            ["-DRV1106_DUAL_IPC=ON", "-DRC0240_LGV10=ON", "-DAIC8800DL=ON", "-Drelease=ON"],
        ),
    ]

    bad_flags: list[str] = []
    with tempfile.TemporaryDirectory(prefix="rk-gb-flags-") as tmp:
        root_build_dir = Path(tmp)
        for project_name, source_dir, extra_options in projects:
            build_dir = root_build_dir / project_name
            configure_project(source_dir, build_dir, extra_options)
            tokens_by_file = collect_compile_flag_tokens(build_dir)

            if not tokens_by_file:
                bad_flags.append(f"{project_name}: no generated flags.make files found")
                continue

            for flags_make, tokens in tokens_by_file.items():
                rel = Path(project_name) / flags_make.relative_to(build_dir)
                if "-o3" in tokens:
                    bad_flags.append(f"{rel}: contains invalid lowercase -o3")
                if "-O3" in tokens:
                    bad_flags.append(f"{rel}: contains uppercase -O3, which is not allowed here")

    if bad_flags:
        print("\n".join(bad_flags), file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
