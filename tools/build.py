import argparse
import os
import subprocess
import sys
import shutil

def main():
    parser = argparse.ArgumentParser(description="Master Build Script for OpenRatchet")
    parser.add_argument('--ps2recomp-dir', default=os.path.join('third_party', 'PS2Recomp'), help='Path to PS2Recomp tools')
    args = parser.parse_args()

    # Step 1: Ensure extraction is done
    if not os.path.exists(os.path.join('data', 'raw', 'SCUS_971.99')):
        print("Extraction needed. Running tools/extract.py...")
        subprocess.run([sys.executable, os.path.join('tools', 'extract.py'), 'extract', '--all'], check=True)

    # Step 2: Ensure analysis is done
    toml_path = os.path.join('data', 'analysis', 'rc1.toml')
    if not os.path.exists(toml_path):
        print("Analysis needed. Running tools/analyze.py...")
        subprocess.run([sys.executable, os.path.join('tools', 'analyze.py')], check=True)

    # Step 3: Run ps2xRecomp
    recomp_exe = os.path.join(args.ps2recomp_dir, 'build', 'ps2xRecomp', 'ps2_recomp.exe')
    if not os.path.exists(recomp_exe):
        recomp_exe = os.path.join('tools', 'ps2xRecomp.exe')
        if not os.path.exists(recomp_exe):
            print(f"Error: {recomp_exe} not found. Please build PS2Recomp or put it in tools/.")
            sys.exit(1)
            
    # Remove ghidra_output from TOML if present, so we can get the base C++ files
    with open(toml_path, 'r', encoding='utf-8') as f:
        toml_content = f.read()
    toml_content = toml_content.replace('ghidra_output', '# ghidra_output')
    with open(toml_path, 'w', encoding='utf-8') as f:
        f.write(toml_content)
        
    print(f"Running initial ps2xRecomp pass...")
    subprocess.run([recomp_exe, toml_path], check=True)
    
    print("Running analyze.py to inject manual exceptions into function map...")
    subprocess.run([sys.executable, os.path.join('tools', 'analyze.py')], check=True)
    
    print(f"Running final ps2xRecomp pass with function map...")
    subprocess.run([recomp_exe, toml_path], check=True)

    # Step 4: Copy generated files to src/recompiled
    output_dir = os.path.join('data', 'analysis', 'output')
    recompiled_dir = os.path.join('src', 'recompiled')
    os.makedirs(recompiled_dir, exist_ok=True)
    
    print(f"Copying generated files from {output_dir} to {recompiled_dir}...")
    count = 0
    for file in os.listdir(output_dir):
        if file.endswith('.cpp') or file.endswith('.h'):
            shutil.copy2(os.path.join(output_dir, file), os.path.join(recompiled_dir, file))
            count += 1
    print(f"Copied {count} files.")

    # Step 5: Configure and build native executable
    print("Building OpenRatchet...")
    subprocess.run(['cmake', '-S', '.', '-B', 'build', '-DOPENRATCHET_SKIP_DEPS=OFF'], check=True)
    subprocess.run(['cmake', '--build', 'build', '--config', 'Release'], check=True)

    print("Build complete! Executable is at build/Release/openratchet.exe")

if __name__ == '__main__':
    main()
