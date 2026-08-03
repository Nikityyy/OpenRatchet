"""
build.py — Master build script for OpenRatchet

Steps:
  1. Ensure game data is extracted (calls extract.py if needed)
  2. Ensure analysis/recompilation is done (calls analyze.py if needed)
  3. Run ps2xRecomp to generate C++ from the MIPS ELF
  4. Copy generated files into src/recompiled/
  5. Configure and build with CMake + Ninja using MSVC developer environment
"""

import argparse
import os
import subprocess
import sys
import shutil

# ── VS Developer Environment Detection ───────────────────────────────────────

def find_vsdevcmd():
    """Return the path to VsDevCmd.bat, or None if not found."""
    candidates = [
        r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat",
        r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
        r"C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat",
    ]
    # Also check via vswhere
    vswhere = shutil.which("vswhere") or r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if os.path.exists(vswhere):
        try:
            result = subprocess.run(
                [vswhere, "-latest", "-property", "installationPath"],
                capture_output=True, text=True, timeout=5
            )
            if result.returncode == 0:
                install_path = result.stdout.strip()
                candidate = os.path.join(install_path, "Common7", "Tools", "VsDevCmd.bat")
                candidates.insert(0, candidate)
        except Exception:
            pass
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def cmake_run(cmake_args, *, vsdevcmd=None):
    """Run a cmake command, optionally wrapped in the VS developer environment."""
    cmake = shutil.which("cmake") or r"C:\Program Files\CMake\bin\cmake.exe"
    cmd   = [cmake] + cmake_args

    if vsdevcmd:
        # Wrap: cmd /c "VsDevCmd.bat" && cmake ...
        shell_cmd = f'"{vsdevcmd}" -arch=x64 -no_logo && ' + " ".join(f'"{a}"' if " " in a else a for a in cmd)
        subprocess.run(["cmd", "/c", shell_cmd], check=True)
    else:
        subprocess.run(cmd, check=True)


def main():
    parser = argparse.ArgumentParser(description="Master Build Script for OpenRatchet")
    parser.add_argument("--ps2recomp-dir",
                        default=os.path.join("third_party", "PS2Recomp"),
                        help="Path to PS2Recomp tools directory")
    parser.add_argument("--skip-recomp", action="store_true",
                        help="Skip ps2xRecomp step (use existing src/recompiled/)")
    parser.add_argument("--build-type", default="Release",
                        choices=["Debug", "Release", "RelWithDebInfo"],
                        help="CMake build type")
    args = parser.parse_args()

    vsdevcmd = find_vsdevcmd()
    if vsdevcmd:
        print(f"Found VS developer environment: {vsdevcmd}")
    else:
        print("Warning: VsDevCmd.bat not found — assuming environment is already set up")

    # Step 1: Ensure extraction is done
    if not os.path.exists(os.path.join("data", "raw", "SCUS_971.99")):
        print("Extraction needed. Running tools/extract.py ...")
        subprocess.run(
            [sys.executable, os.path.join("tools", "extract.py"), "extract", "--all"],
            check=True
        )

    if not args.skip_recomp:
        # Step 2: Ensure analysis is done
        toml_path = os.path.join("data", "analysis", "rc1.toml")
        if not os.path.exists(toml_path):
            print("Analysis needed. Running tools/analyze.py ...")
            subprocess.run(
                [sys.executable, os.path.join("tools", "analyze.py")],
                check=True
            )

        # Step 3: Locate and run ps2xRecomp
        recomp_candidates = [
            os.path.join(args.ps2recomp_dir, "build", "ps2xRecomp", "ps2_recomp.exe"),
            os.path.join(args.ps2recomp_dir, "ps2xRecomp.exe"),
            os.path.join("tools", "ps2xRecomp.exe"),
        ]
        recomp_exe = next((p for p in recomp_candidates if os.path.exists(p)), None)
        if not recomp_exe:
            print("Error: ps2xRecomp.exe not found. Build or download PS2Recomp.")
            sys.exit(1)

        # Temporarily disable ghidra_output so ps2xRecomp uses auto-detected functions first
        with open(toml_path, "r", encoding="utf-8") as f:
            toml_content = f.read()
        patched = toml_content.replace("ghidra_output", "# ghidra_output")
        with open(toml_path, "w", encoding="utf-8") as f:
            f.write(patched)

        print("Running initial ps2xRecomp pass ...")
        subprocess.run([recomp_exe, toml_path], check=True)

        print("Injecting manual function exceptions ...")
        subprocess.run([sys.executable, os.path.join("tools", "analyze.py")], check=True)

        print("Running final ps2xRecomp pass with function map ...")
        subprocess.run([recomp_exe, toml_path], check=True)

        # Step 4: Copy generated files to src/recompiled/
        output_dir    = os.path.join("data", "analysis", "output")
        recompiled_dir = os.path.join("src", "recompiled")
        os.makedirs(recompiled_dir, exist_ok=True)
        count = 0
        for fname in os.listdir(output_dir):
            if fname.endswith(".cpp") or fname.endswith(".h"):
                shutil.copy2(os.path.join(output_dir, fname),
                             os.path.join(recompiled_dir, fname))
                count += 1
        print(f"Copied {count} generated files to src/recompiled/")

    # Step 5: Configure and build with CMake + Ninja
    build_dir = "build"
    print("Configuring CMake ...")
    cmake_run(["-S", ".", "-B", build_dir, "-G", "Ninja",
               f"-DCMAKE_BUILD_TYPE={args.build_type}",
               "-DOPENRATCHET_SKIP_DEPS=OFF"],
              vsdevcmd=vsdevcmd)

    print("Building ...")
    cmake_run(["--build", build_dir, "--config", args.build_type,
               "--parallel"], vsdevcmd=vsdevcmd)

    exe = os.path.join(build_dir, "openratchet.exe")
    if os.path.exists(exe):
        print(f"\nBuild complete! Executable: {exe}")
    else:
        print("\nBuild finished but openratchet.exe not found — check for errors above.")


if __name__ == "__main__":
    main()
