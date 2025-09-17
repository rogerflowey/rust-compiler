# scripts/run_tests.py
import os
import subprocess
import sys
import argparse
import filecmp
import shutil

REGRESSION_LOG_FILE = "regression_failures.log"

def find_rx_files(directory):
    """Finds all .rx files recursively in a given directory."""
    rx_files = []
    for root, _, files in os.walk(directory):
        for file in files:
            if file.endswith(".rx"):
                rx_files.append(os.path.join(root, file))
    return rx_files

def compare_outputs(raw_path, baseline_path, file_type):
    """
    Compares a single raw output file to its baseline counterpart.
    Returns True if they match (or baseline doesn't exist), False otherwise.
    """
    if not os.path.exists(baseline_path):
        print(f"  -> Regression Check ({file_type}): NEW (No baseline found)")
        return True # Not a failure if baseline is new

    if filecmp.cmp(raw_path, baseline_path, shallow=False):
        print(f"  -> Regression Check ({file_type}): PASS")
        return True
    else:
        print(f"  -> Regression Check ({file_type}): FAIL (Output differs from baseline!)")
        return False

def handle_update_baseline(raw_dir, baseline_dir):
    """Copies the current raw output to the baseline directory."""
    print("Updating baseline...")
    if not os.path.isdir(raw_dir):
        print(f"Error: Raw output directory '{raw_dir}' not found.")
        print("Please run the tests at least once before updating the baseline.")
        sys.exit(1)

    if os.path.isdir(baseline_dir):
        print(f"Removing old baseline at '{baseline_dir}'...")
        shutil.rmtree(baseline_dir)
    
    print(f"Copying '{raw_dir}' to '{baseline_dir}'...")
    shutil.copytree(raw_dir, baseline_dir)
    print("Baseline updated successfully.")


def main():
    parser = argparse.ArgumentParser(description="Run compiler tests, capture output, and check for regressions.")
    parser.add_argument("stage", nargs="?", help="The test stage to run (e.g., semantic-1). Runs all if not specified.")
    parser.add_argument("--update-baseline", action="store_true", help="Copy current raw test outputs to the baseline for regression checking.")
    args = parser.parse_args()

    # --- Configuration ---
    base_test_directory = "../RCompiler-Testcases"
    compiler_executable = "../build/ninja-debug/compiler"
    raw_output_directory = "../test-output/raw"
    baseline_directory = "../test-output/baseline"
    # ---------------------

    if args.update_baseline:
        handle_update_baseline(raw_output_directory, baseline_directory)
        return

    test_directory = base_test_directory
    if args.stage:
        test_directory = os.path.join(test_directory, args.stage)

    if not os.path.exists(compiler_executable):
        print(f"Error: Compiler executable not found at {compiler_executable}")
        sys.exit(1)

    if not os.path.isdir(test_directory):
        print(f"Error: Test directory '{test_directory}' not found.")
        sys.exit(1)

    # Clear previous regression log
    if os.path.exists(REGRESSION_LOG_FILE):
        os.remove(REGRESSION_LOG_FILE)

    rx_files = find_rx_files(test_directory)
    if not rx_files:
        print(f"No .rx files found in {test_directory}")
        return

    print(f"Found {len(rx_files)} test(s) to run.")
    regression_failures = set()

    for rx_file in sorted(rx_files):
        print(f"Testing {rx_file}...")
        
        relative_path = os.path.relpath(rx_file, base_test_directory)
        raw_output_path = os.path.join(raw_output_directory, os.path.dirname(relative_path))
        baseline_output_path = os.path.join(baseline_directory, os.path.dirname(relative_path))
        os.makedirs(raw_output_path, exist_ok=True)
        
        base_name = os.path.splitext(os.path.basename(rx_file))[0]
        out_file = os.path.join(raw_output_path, f"{base_name}.out")
        err_file = os.path.join(raw_output_path, f"{base_name}.err")

        try:
            result = subprocess.run(
                [compiler_executable, rx_file], 
                capture_output=True, text=True, check=False
            )
            
            with open(out_file, 'w') as f: f.write(result.stdout)
            with open(err_file, 'w') as f: f.write(result.stderr)

            if result.returncode == 0:
                print("  -> Status: Success (Exit 0)")
            else:
                print(f"  -> Status: Failure (Exit {result.returncode}, check {err_file})")

            # --- REGRESSION CHECK ---
            baseline_out_file = os.path.join(baseline_output_path, f"{base_name}.out")
            baseline_err_file = os.path.join(baseline_output_path, f"{base_name}.err")
            
            out_ok = compare_outputs(out_file, baseline_out_file, ".out")
            err_ok = compare_outputs(err_file, baseline_err_file, ".err")

            if not out_ok or not err_ok:
                regression_failures.add(base_name)

        except FileNotFoundError:
             print(f"Error: Command not found. Is your compiler at '{compiler_executable}'?")
             sys.exit(1)
        except Exception as e:
            print(f"An unexpected error occurred while running {rx_file}: {e}")

    if regression_failures:
        print(f"\nDetected {len(regression_failures)} regression failure(s). Writing to '{REGRESSION_LOG_FILE}'.")
        with open(REGRESSION_LOG_FILE, 'w') as f:
            for name in sorted(list(regression_failures)):
                f.write(f"{name}\n")

if __name__ == "__main__":
    main()