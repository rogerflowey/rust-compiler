# scripts/generate_report.py
import os
import argparse
import concurrent.futures
import shutil
import time
from ai_analyzer import analyze_batch_with_ai

# Default log file names
AI_FAIL_LOG = "ai_failures.log"
AI_FAILURE_REPORT = "ai_failure_report.md"
REGRESSION_LOG_FILE = "regression_failures.log"
AI_LOG_DIR = "../test-output/ai-logs"

# Configuration for parallel requests
MAX_WORKERS = 4 
REQUEST_DELAY_SECONDS = 0.2

def find_rx_files(directory):
    """Finds all .rx files recursively in a given directory."""
    rx_files = []
    for root, _, files in os.walk(directory):
        for file in files:
            if file.endswith(".rx"):
                rx_files.append(os.path.join(root, file))
    return rx_files

def main():
    parser = argparse.ArgumentParser(description="Generate reports and analyze compiler outputs.")
    parser.add_argument("stage", help="The test stage to process (e.g., semantic-1).")
    parser.add_argument("--analyze", action="store_true", help="Enable analysis of test outputs with the AI.")
    parser.add_argument("--use-fail-list", action="store_true", help="Analyze only tests from the default failure lists.")
    args = parser.parse_args()

    base_test_directory = "../RCompiler-Testcases"
    raw_output_directory = "../test-output/raw"
    report_directory = "../test-output/report"
    batch_size = 10

    stage_test_dir = os.path.join(base_test_directory, args.stage)
    stage_report_dir = os.path.join(report_directory, args.stage)
    os.makedirs(stage_report_dir, exist_ok=True)

    if not os.path.isdir(stage_test_dir):
        print(f"Error: Test directory not found at {stage_test_dir}")
        return

    all_rx_files = sorted(find_rx_files(stage_test_dir))
    file_map = {os.path.splitext(os.path.basename(f))[0]: f for f in all_rx_files}
    
    tests_to_process = []
    if args.use_fail_list:
        tests_to_process_names = set()
        if os.path.exists(AI_FAIL_LOG):
            print(f"Reading AI failures from {AI_FAIL_LOG}...")
            with open(AI_FAIL_LOG, 'r', encoding='utf-8') as f:
                tests_to_process_names.update(line.strip() for line in f)
        if os.path.exists(REGRESSION_LOG_FILE):
            print(f"Reading regression failures from {REGRESSION_LOG_FILE}...")
            with open(REGRESSION_LOG_FILE, 'r', encoding='utf-8') as f:
                tests_to_process_names.update(line.strip() for line in f)
        if not tests_to_process_names:
            print("Warning: --use-fail-list was specified, but no failure logs were found or they were empty.")
        else:
            print(f"Found {len(tests_to_process_names)} unique tests to process from failure lists.")
        for test_name in sorted(list(tests_to_process_names)):
            if test_name in file_map:
                tests_to_process.append(file_map[test_name])
            else:
                print(f"Warning: Test '{test_name}' from a failure list not found in stage '{args.stage}'.")
    else:
        print("Processing all tests for the stage. (Use --use-fail-list to target previous failures).")
        tests_to_process = all_rx_files

    if not tests_to_process:
        print("No tests to process.")
        return

    batches_to_analyze = []
    for i in range(0, len(tests_to_process), batch_size):
        batch_num = (i // batch_size) + 1
        report_file_path = os.path.join(stage_report_dir, f"report_batch_{batch_num}.txt")
        batch_files = tests_to_process[i:i + batch_size]
        batch_data_for_report = {}
        print(f"Generating {report_file_path}...")
        with open(report_file_path, 'w', encoding='utf-8') as report_file:
            for rx_file in batch_files:
                base_name = os.path.splitext(os.path.basename(rx_file))[0]
                try:
                    with open(rx_file, 'r', encoding='utf-8') as f: source_code = f.read()
                except FileNotFoundError:
                    print(f"Warning: Source file {rx_file} not found. Skipping.")
                    continue
                
                # --- UPDATED LOGIC to correctly find nested output files ---
                relative_path = os.path.relpath(rx_file, base_test_directory)
                output_dir_for_test = os.path.join(raw_output_directory, os.path.dirname(relative_path))
                
                out_file_path = os.path.join(output_dir_for_test, f"{base_name}.out")
                err_file_path = os.path.join(output_dir_for_test, f"{base_name}.err")
                # --- END UPDATED LOGIC ---

                output_content = ""
                error_content = ""
                found_output = False

                if os.path.exists(out_file_path):
                    found_output = True
                    with open(out_file_path, 'r', encoding='utf-8') as f: output_content = f.read().strip()
                if os.path.exists(err_file_path):
                    found_output = True
                    with open(err_file_path, 'r', encoding='utf-8') as f: error_content = f.read().strip()
                
                # --- NEW: Add warning if no output files are found ---
                if not found_output:
                    print(f"  -> WARNING: No .out or .err file found for test '{base_name}'.")

                compiler_output_str = ""
                if error_content: compiler_output_str = f"stderr:\n{error_content}"
                elif output_content: compiler_output_str = f"stdout:\n{output_content}"
                else: compiler_output_str = "stdout: (empty)\nstderr: (empty)"
                
                batch_data_for_report[base_name] = {"source": source_code, "output": compiler_output_str}
                report_file.write(f"====== TEST: {base_name} ======\n--- SOURCE ---\n{source_code}\n--- OUTPUT ---\n{compiler_output_str}\n\n")
        batches_to_analyze.append({"num": batch_num, "path": report_file_path, "data": batch_data_for_report})

    if args.analyze:
        if os.path.isdir(AI_LOG_DIR):
            print(f"Clearing old AI logs in '{AI_LOG_DIR}'...")
            shutil.rmtree(AI_LOG_DIR)
        os.makedirs(AI_LOG_DIR, exist_ok=True)

        detailed_failures = []
        print(f"\nStarting parallel analysis with up to {MAX_WORKERS} workers and a {REQUEST_DELAY_SECONDS}s delay between requests...")
        with concurrent.futures.ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
            future_to_batch = {}
            for batch_info in batches_to_analyze:
                with open(batch_info["path"], 'r', encoding='utf-8') as f:
                    batch_content = f.read()
                future = executor.submit(analyze_batch_with_ai, batch_content, args.stage, batch_info["num"])
                future_to_batch[future] = batch_info
                time.sleep(REQUEST_DELAY_SECONDS)

            for future in concurrent.futures.as_completed(future_to_batch):
                batch_info = future_to_batch[future]
                batch_num = batch_info["num"]
                batch_data_for_report = batch_info["data"]
                try:
                    analysis_results = future.result()
                    print(f"Completed analysis for batch {batch_num}.")
                    batch_test_names = batch_data_for_report.keys()
                    for name in batch_test_names:
                        if name not in analysis_results:
                            print(f"  -> WARNING: No analysis verdict returned for {name} in batch {batch_num}. Assuming failure.")
                            detailed_failures.append({"name": name, "reason": "AI did not return a valid analysis block for this test case.", **batch_data_for_report.get(name, {})})
                        else:
                            is_correct, reason = analysis_results[name]
                            if not is_correct:
                                detailed_failures.append({"name": name, "reason": reason, **batch_data_for_report.get(name, {})})
                except Exception as exc:
                    print(f"Batch {batch_num} generated an exception: {exc}")
                    for name in batch_data_for_report.keys():
                        detailed_failures.append({"name": name, "reason": f"The analysis API call for this batch failed with exception: {exc}", **batch_data_for_report.get(name, {})})

        analysis_failure_names = sorted([f["name"] for f in detailed_failures])
        print(f"\nWriting {len(analysis_failure_names)} failure name(s) to '{AI_FAIL_LOG}'.")
        with open(AI_FAIL_LOG, 'w', encoding='utf-8') as f:
            for name in analysis_failure_names:
                f.write(f"{name}\n")

        print(f"Writing detailed failure report to '{AI_FAILURE_REPORT}'...")
        with open(AI_FAILURE_REPORT, 'w', encoding='utf-8') as f:
            if not detailed_failures:
                f.write("# AI Analysis Report: All Processed Tests Passed!\n")
            else:
                f.write(f"# AI Analysis Failure Report for Stage: {args.stage}\nFound {len(detailed_failures)} failure(s).\n\n")
                for failure in sorted(detailed_failures, key=lambda x: x['name']):
                    f.write(f"## FAILED: {failure['name']}\n\n### AI's Reason\n{failure['reason']}\n\n### Source Code\n```rust\n{failure['source']}\n```\n\n### Compiler Output\n```\n{failure['output']}\n```\n\n---\n\n")

    print("Processing complete.")

if __name__ == "__main__":
    main()