# Compiler Test & Analysis Scripts

This directory contains a suite of Python scripts designed to automate the testing of the Mini-Rust compiler, manage test baselines, and leverage AI to analyze test failures.

## Prerequisites

- Python 3.x
- The Zhipu AI Python library (`pip install zhipuai`)
- An environment variable `ZHIPU_API_KEY` must be set with a valid API key for AI analysis.

## Scripts

### `run_tests.py`

This script is the primary test runner. It executes the compiler against test cases and manages output for regression testing.

**Usage:**

```bash
python run_tests.py [stage] [--update-baseline]
```

-   **`[stage]`** (Optional): The specific test stage to run (e.g., `semantic-1`). If omitted, it runs tests from all stages found in `../RCompiler-Testcases`.
-   **`--update-baseline`**: A flag to promote the most recent test outputs (from `../test-output/raw`) to be the new "golden" standard for regression checks (in `../test-output/baseline`).

**Functionality:**

1.  Finds all `.rx` test files in the specified stage directory.
2.  Executes the compiler (`../build/ninja-debug/compiler`) for each test case.
3.  Saves the `stdout` and `stderr` of the compiler to `.out` and `.err` files in `../test-output/raw/`.
4.  Compares the new output against the corresponding files in `../test-output/baseline/`.
5.  Reports any differences as regression failures in `regression_failures.log`.

---

### `generate_report.py`

This script generates consolidated reports from test outputs and can optionally send them to an AI for analysis.

**Usage:**

```bash
python generate_report.py <stage> [--analyze] [--use-fail-list]
```

-   **`<stage>`**: **(Required)** The test stage to process (e.g., `semantic-1`).
-   **`--analyze`**: Enables the AI analysis feature. It sends the generated reports to the Zhipu AI API for a verdict on each test case.
-   **`--use-fail-list`**: When used with `--analyze`, this flag instructs the script to only analyze tests listed in `ai_failures.log` or `regression_failures.log`. This is useful for re-analyzing only the previously failing tests.

**Functionality:**

1.  **Report Generation**: It gathers the source code (`.rx`), `stdout` (`.out`), and `stderr` (`.err`) for each test in the stage. It then compiles them into batches (e.g., `report_batch_1.txt`) and saves them in `../test-output/report/<stage>/`.
2.  **AI Analysis (`--analyze`)**:
    -   Sends each report batch to the Zhipu AI API using the prompt from `prompts/parser_verifier.md`.
    -   The AI provides a `Verdict` (CORRECT/INCORRECT) and a `Reason` for each test case.
    -   The raw AI responses are logged in `../test-output/ai-logs/`.
    -   A list of all test cases the AI deemed incorrect is saved to `ai_failures.log`.
    -   A detailed markdown report, `ai_failure_report.md`, is generated, showing the source code, compiler output, and AI's reasoning for each failure.

---

### `ai_analyzer.py`

This is a helper module used by `generate_report.py`. It contains the core logic for interacting with the Zhipu AI API, sending prompts, and parsing the structured responses. It is not intended to be run directly.

## Typical Workflow

### 1. Run the Tests

First, run the compiler against the test suite to generate the latest output.

```bash
# Run tests for the 'semantic-1' stage
python scripts/run_tests.py semantic-1
```

This will create `.out` and `.err` files in `../test-output/raw/semantic-1/`. If a baseline exists, it will also check for regressions.

### 2. Analyze Failures with AI

To understand why certain tests are failing, use the `generate_report.py` script with the `--analyze` flag.

```bash
# Analyze all tests in the 'semantic-1' stage
python scripts/generate_report.py semantic-1 --analyze
```

After the analysis is complete, you can review:
-   `ai_failure_report.md`: For a human-readable report of the failures.
-   `ai_failures.log`: For a simple list of failing test names.

### 3. Re-analyze Previous Failures

If you've made fixes and want to quickly check if the previous failures are resolved, use the `--use-fail-list` flag. This avoids spending time and tokens analyzing tests that were already passing.

```bash
# First, run the tests again to get fresh output
python scripts/run_tests.py semantic-1

# Then, re-analyze only the tests that failed last time
python scripts/generate_report.py semantic-1 --analyze --use-fail-list
```

### 4. Update the Baseline

Once you have verified that changes in test output are correct and intentional (e.g., after fixing a bug or adding a feature), update the baseline.

```bash
python scripts/run_tests.py --update-baseline
```

This command copies the entire `../test-output/raw` directory to `../test-output/baseline`, establishing a new ground truth for future regression tests.
