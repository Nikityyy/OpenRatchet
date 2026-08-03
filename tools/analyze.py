import argparse
import os
import subprocess
import sys
import re
import csv

# Default: ps2xAnalyzer.exe sits next to this script in tools/
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_DEFAULT_ANALYZER = os.path.join(_SCRIPT_DIR, "ps2xAnalyzer.exe")

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
    except FileNotFoundError as exc:
        raise RuntimeError(
            f"ps2xAnalyzer was not found: {analyzer_path}. "
            "Install/build PS2Recomp or pass --analyzer PATH."
        ) from exc

    return toml_path

import struct

def find_jal_targets(elf_path):
    targets = set()
    try:
        with open(elf_path, 'rb') as f:
            header = f.read(52)
            if header[0:4] != b'\x7fELF':
                return targets
            phoff = struct.unpack_from('<I', header, 28)[0]
            phentsize = struct.unpack_from('<H', header, 42)[0]
            phnum = struct.unpack_from('<H', header, 44)[0]
            
            for i in range(phnum):
                f.seek(phoff + i * phentsize)
                p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack('<IIIIIIII', f.read(32))
                if p_type == 1 and (p_flags & 1):  # PT_LOAD and Executable
                    f.seek(p_offset)
                    data = f.read(p_filesz)
                    for j in range(0, len(data) - 3, 4):
                        instr = struct.unpack_from('<I', data, j)[0]
                        opcode = instr >> 26
                        if opcode == 3: # JAL
                            target = ((instr & 0x03FFFFFF) << 2) | ((p_vaddr + j) & 0xF0000000)
                            targets.add(target)
    except Exception as e:
        print(f"Failed to extract JAL targets: {e}")
    return targets

def make_function_map(elf_path, output_dir, output_csv):
    output_cpp_dir = os.path.join(output_dir, 'output')
    functions = []
    known_addrs = set()
    
    if os.path.exists(output_cpp_dir):
        # Match // Function: name\n// Address: 0x... - 0x...
        pattern1 = re.compile(r"^// Function: (.+)\n// Address: 0x([0-9a-fA-F]+) - 0x([0-9a-fA-F]+)", re.MULTILINE)
        # Match void sub_ADDRESS_0xADDRESS(...)
        pattern2 = re.compile(r"void\s+(sub_([0-9a-fA-F]+)_(0x[0-9a-fA-F]+))\(")
        
        for root, _, files in os.walk(output_cpp_dir):
            for file in files:
                if file.endswith('.cpp'):
                    with open(os.path.join(root, file), 'r', encoding='utf-8') as f:
                        content = f.read()
                        
                        # Process pattern 1
                        for match in pattern1.finditer(content):
                            name, start, end = match.groups()
                            start_val = int(start, 16)
                            if start_val not in known_addrs:
                                size_val = int(end, 16) - start_val
                                size_hex = f"0x{size_val:X}"
                                functions.append({
                                    'name': name,
                                    'address': f"0x{start.upper()}",
                                    'end': f"0x{end.upper()}",
                                    'size': size_hex
                                })
                                known_addrs.add(start_val)
                                
                        # Process pattern 2 for stubs
                        for match in pattern2.finditer(content):
                            name, start_str, _ = match.groups()
                            start_val = int(start_str, 16)
                            if start_val not in known_addrs:
                                functions.append({
                                    'name': name,
                                    'address': f"0x{start_str.upper()}",
                                    'end': "0x0",
                                    'size': "0x0"
                                })
                                known_addrs.add(start_val)

    # Now add missing JAL targets
    jal_targets = find_jal_targets(elf_path)
    for target in jal_targets:
        if target not in known_addrs and 0x00100000 <= target < 0x02000000:
            name = f"sub_{target:08X}_0x{target:x}"
            functions.append({
                'name': name,
                'address': f"0x{target:08X}",
                'end': "0x0",
                'size': "0x0"
            })
            known_addrs.add(target)
                            
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
    parser.add_argument('--analyzer', default=_DEFAULT_ANALYZER, help='Path to ps2xAnalyzer executable')
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
    make_function_map(args.elf, args.output, auto_map_csv)
    
    toml_path = os.path.join(args.output, 'rc1.toml')
    prepare_recomp_config(toml_path, auto_map_csv)

if __name__ == '__main__':
    main()
