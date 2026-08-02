#!/usr/bin/env python3
"""Build and run the native PS2 recompilation from a user-provided ISO."""

from __future__ import annotations

import argparse
import csv
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
BOOT = DATA / "raw" / "SCUS_971.99"
ANALYSIS = DATA / "analysis"
RECOMP = ROOT / "third_party" / "PS2Recomp"
BUILD = ROOT / "build" / "ps2recomp-ninja"


def run(command: list[str]) -> None:
    print("+", " ".join(command))
    subprocess.run(command, cwd=ROOT, check=True)


def cmake(command: list[str]) -> None:
    vsdev = next(
        Path("C:/Program Files/Microsoft Visual Studio").glob(
            "*/Community/Common7/Tools/VsDevCmd.bat"
        ),
        None,
    )
    if vsdev:
        line = subprocess.list2cmdline(command)
        print("+", line)
        subprocess.run(
            f'"{vsdev}" -arch=x64 -host_arch=x64 && {line}',
            cwd=ROOT,
            check=True,
            shell=True,
        )
    else:
        run(command)


def extract_if_needed() -> None:
    if not BOOT.is_file():
        run([sys.executable, str(ROOT / "tools" / "openratchet.py"), "extract"])


def write_auto_config(path: Path) -> None:
    path.write_text(
        """[general]
input = "data/raw/SCUS_971.99"
output = "data/analysis/auto-output/"
single_file_output = false
patch_syscalls = false
patch_cop0 = true
patch_cache = true
""",
        encoding="utf-8",
    )


def make_map(auto_output: Path, destination: Path) -> None:
    rows: dict[int, tuple[str, int, int]] = {}
    pattern = re.compile(r"(?m)^// Function: (.+)\n// Address: 0x([0-9a-f]+) - 0x([0-9a-f]+)")
    for source in auto_output.glob("*.cpp"):
        match = pattern.search(source.read_text(encoding="utf-8"))
        if not match:
            continue
        name, start_text, end_text = match.groups()
        start = int(start_text, 16)
        end = int(end_text, 16)
        rows[start] = (name, end, end - start)

    # ponytail: these are the only two indirect/jump-table entries the scanner misses;
    # replace with Ghidra before expanding beyond this bring-up profile.
    rows[0x11DC18] = ("sub_0011DC18", 0x11DCC8, 0xB0)
    rows[0x1E9488] = ("sub_001E9488", 0x1E9658, 0x1D0)
    rows[0x1E9658] = ("sub_001E9658", 0x1E9AB8, 0x460)

    with destination.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(("name", "address", "end", "size"))
        for start in sorted(rows):
            name, end, size = rows[start]
            writer.writerow((name, f"0x{start:08X}", f"0x{end:08X}", size))


def prepare_config(config: Path) -> None:
    text = config.read_text(encoding="utf-8")
    text = re.sub(r'^ghidra_output\s*=.*\n', "", text, flags=re.MULTILINE)
    text = re.sub(
        r'(?m)^(input\s*=.*\n)',
        r'\1ghidra_output = "data/analysis/auto-map.csv"\n',
        text,
        count=1,
    )
    text = re.sub(r'^\s*"InitExecPS2@0x0011D9B8",\n', "", text, flags=re.MULTILINE)
    config.write_text(text, encoding="utf-8")


def clean_generated() -> None:
    for directory in (ANALYSIS / "output", ANALYSIS / "auto-output"):
        if directory.exists():
            shutil.rmtree(directory)
    (ANALYSIS / "output").mkdir(parents=True)


def build() -> Path:
    extract_if_needed()
    for executable in (
        BUILD / "ps2xAnalyzer" / "ps2_analyzer.exe",
        BUILD / "ps2xRecomp" / "ps2_recomp.exe",
    ):
        if not executable.is_file():
            raise SystemExit(f"missing {executable}; build third_party/PS2Recomp first")

    ANALYSIS.mkdir(parents=True, exist_ok=True)
    clean_generated()
    config = ANALYSIS / "rc1.toml"
    auto_config = ANALYSIS / "rc1-auto.toml"
    function_map = ANALYSIS / "auto-map.csv"

    run([str(BUILD / "ps2xAnalyzer" / "ps2_analyzer.exe"), str(BOOT), str(config)])
    write_auto_config(auto_config)
    run([str(BUILD / "ps2xRecomp" / "ps2_recomp.exe"), str(auto_config)])
    make_map(ANALYSIS / "auto-output", function_map)
    prepare_config(config)
    run([str(BUILD / "ps2xRecomp" / "ps2_recomp.exe"), str(config)])

    runner_source = RECOMP / "ps2xRuntime" / "src" / "runner"
    runner_include = RECOMP / "ps2xRuntime" / "include"
    for source in runner_source.glob("*.cpp"):
        source.unlink()
    for header in runner_include.glob("ps2_recompiled_*.h"):
        header.unlink()
    for source in (ANALYSIS / "output").glob("*.cpp"):
        shutil.copy2(source, runner_source / source.name)
    for header in (ANALYSIS / "output").glob("*.h"):
        shutil.copy2(header, runner_include / header.name)

    cmake(
        [
            "cmake",
            "-S",
            str(RECOMP),
            "-B",
            str(ROOT / "build" / "ps2recomp-ninja"),
            "-G",
            "Ninja",
            "-DPS2X_BUILD_STUDIO=OFF",
            "-DPS2X_BUILD_TEST=OFF",
            "-DFETCHCONTENT_FULLY_DISCONNECTED=ON",
        ]
    )
    cmake(
        [
            "cmake",
            "--build",
            str(ROOT / "build" / "ps2recomp-ninja"),
            "--target",
            "ps2EntryRunner",
            "-j",
            "8",
        ]
    )
    return ROOT / "build" / "ps2recomp-ninja" / "ps2xRuntime" / "ps2EntryRunner.exe"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("build", "run"))
    args = parser.parse_args()
    runner = build() if args.command == "build" else ROOT / "build" / "ps2recomp-ninja" / "ps2xRuntime" / "ps2EntryRunner.exe"
    if args.command == "run":
        if not runner.is_file():
            runner = build()
        subprocess.run([str(runner), str(BOOT)], cwd=ROOT, check=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
