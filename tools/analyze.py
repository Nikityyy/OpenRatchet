import argparse
import os
import subprocess
import sys
import re
import csv

def ensure_elf(elf_path):
    if not os.path.exists(elf_path):
        print(f"Warning: {elf_path} not found. Attempting to run tools/extract.py...")
        cmd = [sys.executable, os.path.join('tools', 'extract.py'), 'extract', '--all']
        print(f"Running: {' '.join(cmd)}")
        try:
            subprocess.run(cmd, check=True)
        except subprocess.CalledProcessError:
            print(f"Error: Failed to extract ELF. Please run tools/extract.py manually.")
            sys.exit(1)
        
        if not os.path.exists(elf_path):
            print(f"Error: {elf_path} still not found after extraction.")
            sys.exit(1)

def run_ghidra(ghidra_home, elf_path, output_dir):
    analyze_headless = os.path.join(ghidra_home, 'support', 'analyzeHeadless')
    if os.name == 'nt':
        analyze_headless += '.bat'
    
    if not os.path.exists(analyze_headless):
        print(f"Error: {analyze_headless} not found.")
        sys.exit(1)

    project_dir = os.path.join(output_dir, 'ghidra_proj')
    os.makedirs(project_dir, exist_ok=True)
    
    export_output = os.path.join(output_dir, 'ghidra_export.txt')
    
    cmd = [
        analyze_headless,
        project_dir,
        'OpenRatchet',
        '-import', elf_path,
        '-processor', 'MIPS:LE:32:R5900',
        '-postScript', 'ExportPS2Functions.java', export_output,
        '-overwrite'
    ]
    print(f"Running Ghidra analysis: {' '.join(cmd)}")
    subprocess.run(cmd, check=True)
    return export_output

def run_ps2xanalyzer(analyzer_path, elf_path, output_dir):
    toml_path = os.path.join(output_dir, 'rc1.toml')
    os.makedirs(output_dir, exist_ok=True)
    
    cmd = [analyzer_path, elf_path, toml_path]
    print(f"Running ps2xAnalyzer: {' '.join(cmd)}")
    try:
        subprocess.run(cmd, check=True)
    except FileNotFoundError:
        print(f"Warning: {analyzer_path} not found. Ensure it is built or available in PATH.")
        # We'll create a dummy TOML for now to allow the script to proceed if analyzer is missing
        with open(toml_path, 'w', encoding='utf-8') as f:
            f.write('[general]\n')
            f.write(f'input = "{elf_path.replace("\\\\", "/")}"\n')
            f.write(f'output = "{output_dir.replace("\\\\", "/")}/output/"\n')
            f.write('single_file_output = false\n')
            f.write('patch_syscalls = false\n')
            f.write('patch_cop0 = true\n')
            f.write('patch_cache = true\n')

    return toml_path

def make_function_map(output_dir, output_csv):
    output_cpp_dir = os.path.join(output_dir, 'output')
    functions = []
    
    # Known manual exceptions for the R&C1 stripped ELF
    exceptions = [
        {'name': 'sub_0011DC18', 'address': '0x0011DC18', 'end': '0x0011DCC8', 'size': '0xB0'},
        {'name': 'sub_001E9488', 'address': '0x001E9488', 'end': '0x001E9658', 'size': '0x1D0'},
        {'name': 'sub_001E9658', 'address': '0x001E9658', 'end': '0x001E9AB8', 'size': '0x460'}
    ]
    functions.extend(exceptions)
    
    if os.path.exists(output_cpp_dir):
        pattern = re.compile(r"^// Function: (.+)\n// Address: 0x([0-9a-fA-F]+) - 0x([0-9a-fA-F]+)", re.MULTILINE)
        for root, _, files in os.walk(output_cpp_dir):
            for file in files:
                if file.endswith('.cpp'):
                    with open(os.path.join(root, file), 'r', encoding='utf-8') as f:
                        content = f.read()
                        for match in pattern.finditer(content):
                            name, start, end = match.groups()
                            size_val = int(end, 16) - int(start, 16)
                            size_hex = f"0x{size_val:X}"
                            functions.append({
                                'name': name,
                                'address': f"0x{start.upper()}",
                                'end': f"0x{end.upper()}",
                                'size': size_hex
                            })
                            
    # Write CSV
    with open(output_csv, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=['name', 'address', 'end', 'size'])
        writer.writeheader()
        for func in functions:
            writer.writerow(func)
            
    print(f"Generated function map at {output_csv} with {len(functions)} entries.")

def prepare_recomp_config(toml_path, csv_path):
    if not os.path.exists(toml_path):
        print(f"Warning: TOML config {toml_path} not found. Cannot inject ghidra_output.")
        return

    with open(toml_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()
        
    out_lines = []
    has_ghidra_output = False
    
    for line in lines:
        if line.strip().startswith('ghidra_output'):
            has_ghidra_output = True
            out_lines.append(f'ghidra_output = "{csv_path.replace("\\", "/")}"\n')
            continue
            
        if 'InitExecPS2@0x0011D9B8' in line:
            continue
            
        out_lines.append(line)
        
    if not has_ghidra_output:
        # insert ghidra_output in [general]
        for i, line in enumerate(out_lines):
            if line.strip() == '[general]':
                out_lines.insert(i + 1, f'ghidra_output = "{csv_path.replace("\\", "/")}"\n')
                break
                
    with open(toml_path, 'w', encoding='utf-8') as f:
        f.writelines(out_lines)
        
    print(f"Updated config {toml_path} with ghidra_output={csv_path}")

def main():
    parser = argparse.ArgumentParser(description="Ghidra Analysis and Function Map Generation")
    parser.add_argument('--elf', default='data/raw/SCUS_971.99', help='Path to the ELF file')
    parser.add_argument('--ghidra', default=os.environ.get('GHIDRA_HOME', ''), help='Path to Ghidra home directory')
    parser.add_argument('--analyzer', default='ps2xAnalyzer.exe', help='Path to ps2xAnalyzer executable')
    parser.add_argument('--output', default='data/analysis', help='Output directory')
    
    args = parser.parse_args()
    
    ensure_elf(args.elf)
    os.makedirs(args.output, exist_ok=True)
    
    if args.ghidra and os.path.exists(args.ghidra):
        run_ghidra(args.ghidra, args.elf, args.output)
    else:
        print("Ghidra not found or not specified, falling back to ps2xAnalyzer.")
        run_ps2xanalyzer(args.analyzer, args.elf, args.output)
        
    auto_map_csv = os.path.join(args.output, 'auto-map.csv')
    make_function_map(args.output, auto_map_csv)
    
    toml_path = os.path.join(args.output, 'rc1.toml')
    prepare_recomp_config(toml_path, auto_map_csv)

if __name__ == '__main__':
    main()
