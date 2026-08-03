import argparse
import subprocess
import time
import sys

def main():
    parser = argparse.ArgumentParser(description="Smoke test for OpenRatchet")
    parser.add_argument('--seconds', type=int, default=5, help='Seconds to wait')
    parser.add_argument('--exe', default='build/Release/openratchet.exe', help='Path to executable')
    args = parser.parse_args()

    print(f"Running {args.exe} for {args.seconds} seconds...")
    try:
        proc = subprocess.Popen([args.exe, "data/raw/SCUS_971.99"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(args.seconds)
        
        if proc.poll() is None:
            print("Process is still running. Terminating...")
            proc.terminate()
            stdout, stderr = proc.communicate(timeout=2)
            stderr_str = stderr.decode(errors='replace')
            stdout_str = stdout.decode(errors='replace')
            
            error_keywords = ['MISSING-TARGET', 'FATAL', 'UNIMPLEMENTED', 'CRASH', 'segfault']
            found_errors = [kw for kw in error_keywords if kw.lower() in stderr_str.lower() or kw.lower() in stdout_str.lower()]
            
            if found_errors:
                print(f"Found error keywords: {found_errors}")
                print("--- STDERR ---")
                print(stderr_str)
                print("smoke_test: FAIL")
                sys.exit(1)
            else:
                print("smoke_test: PASS")
                sys.exit(0)
        elif proc.returncode == 0:
            stdout, stderr = proc.communicate()
            print("Process exited successfully.")
            print("--- STDOUT ---")
            print(stdout.decode(errors='replace'))
            print("smoke_test: PASS")
            sys.exit(0)
        else:
            stdout, stderr = proc.communicate()
            print(f"Process crashed with exit code {proc.returncode}")
            print("--- STDERR ---")
            print(stderr.decode(errors='replace'))
            print("--- STDOUT ---")
            print(stdout.decode(errors='replace'))
            print("smoke_test: FAIL")
            sys.exit(1)
            
    except Exception as e:
        print(f"Failed to run executable: {e}")
        sys.exit(1)

if __name__ == '__main__':
    main()
