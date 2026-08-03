import argparse
import subprocess
import time
import sys
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(description="Smoke test for OpenRatchet")
    parser.add_argument('--seconds', type=int, default=5, help='Seconds to wait')
    parser.add_argument('--exe', default=None, help='Path to executable')
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    candidates = [
        Path(args.exe) if args.exe else None,
        root / 'build' / 'openratchet.exe',
        root / 'build' / 'Release' / 'openratchet.exe',
    ]
    exe = next((p.resolve() for p in candidates if p and p.exists()), None)
    if exe is None:
        print('Failed to run executable: no build/openratchet.exe or build/Release/openratchet.exe found')
        sys.exit(1)

    print(f"Running {exe} for {args.seconds} seconds...")
    try:
        proc = subprocess.Popen(
            [str(exe), str(root / 'data' / 'raw' / 'SCUS_971.99')],
            cwd=root,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        try:
            output, _ = proc.communicate(timeout=args.seconds)
        except subprocess.TimeoutExpired:
            proc.terminate()
            output, _ = proc.communicate(timeout=2)
            if proc.returncode not in (None, 0):
                print(f"Process terminated with exit code {proc.returncode}")
                print(output)
                print("smoke_test: FAIL")
                sys.exit(1)

            error_keywords = ['MISSING-TARGET', 'FATAL', 'UNIMPLEMENTED', 'CRASH', 'segfault', 'Failed to']
            found_errors = [kw for kw in error_keywords if kw.lower() in output.lower()]
            if found_errors:
                print(f"Found error keywords: {found_errors}")
                print(output)
                print("smoke_test: FAIL")
                sys.exit(1)
            print("smoke_test: PASS")
            sys.exit(0)

        if proc.returncode == 0:
            print("Process exited before the observation period")
        else:
            print(f"Process crashed with exit code {proc.returncode}")
        print(output)
        print("smoke_test: FAIL")
        sys.exit(1)
            
    except Exception as e:
        print(f"Failed to run executable: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
