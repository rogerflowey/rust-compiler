#!/usr/bin/env python3
"""Run semantic pipeline across a test set and capture outputs."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser(
		description="Run semantic_pipeline across all .rx files in a test set.",
	)
	parser.add_argument(
		"test_set",
		help="Name of the test set under RCompiler-Testcases (e.g. semantic-1)",
	)
	parser.add_argument(
		"--binary",
		default="build/ninja-debug/cmd/semantic_pipeline",
		help="Path to semantic_pipeline binary relative to the repository root.",
	)
	parser.add_argument(
		"--test-root",
		default="RCompiler-Testcases",
		help="Directory containing test sets relative to the repository root.",
	)
	return parser.parse_args()


RED = "\033[31m"
RESET = "\033[0m"


VERDICT_SUCCESS_MAP = {
	"pass": True,
	"success": True,
	"ok": True,
	"fail": False,
	"failure": False,
	"error": False,
}


def discover_cases(src_dir: Path) -> list[Path]:
	return sorted(src_dir.rglob("*.rx"))


def parse_verdict(case_path: Path) -> str:
	for line in case_path.read_text(encoding="utf-8").splitlines():
		stripped = line.strip()
		if stripped.lower().startswith("verdict:"):
			return stripped.split(":", 1)[1].strip()
	raise ValueError(f"verdict not found in {case_path}")


def verdict_to_success(verdict: str) -> bool:
	key = verdict.lower()
	if key in VERDICT_SUCCESS_MAP:
		return VERDICT_SUCCESS_MAP[key]
	raise ValueError(f"unsupported verdict '{verdict}'")


def extract_last_line(text: str) -> str:
	lines = text.rstrip().splitlines()
	return lines[-1] if lines else "<no output>"


def run_case(binary_path: Path, case_path: Path) -> subprocess.CompletedProcess[str]:
	return subprocess.run(
		[str(binary_path), str(case_path)],
		capture_output=True,
		text=True,
	)


def main() -> int:
	args = parse_args()
	repo_root = Path(__file__).resolve().parent.parent

	binary_path = (repo_root / args.binary).resolve()
	if not binary_path.is_file():
		sys.stderr.write(f"error: binary not found: {binary_path}\n")
		return 1

	test_set_dir = (repo_root / args.test_root / args.test_set).resolve()
	src_dir = test_set_dir / "src"
	if not src_dir.is_dir():
		sys.stderr.write(f"error: src directory not found: {src_dir}\n")
		return 1

	cases = discover_cases(src_dir)
	if not cases:
		sys.stderr.write(f"warning: no .rx files found under {src_dir}\n")
		return 0

	output_root = repo_root / "scripts" / "output" / args.test_set
	output_root.mkdir(parents=True, exist_ok=True)
	total_cases = len(cases)
	print(f"Running {total_cases} cases from {src_dir} using {binary_path}")

	mismatches: list[tuple[Path, Path, str, bool, bool, int, str]] = []
	for index, case_path in enumerate(cases, start=1):
		rel_case = case_path.relative_to(src_dir)
		out_path = output_root / rel_case.with_suffix(".out")
		out_path.parent.mkdir(parents=True, exist_ok=True)

		try:
			verdict_text = parse_verdict(case_path)
			expected_success = verdict_to_success(verdict_text)
		except ValueError as exc:
			sys.stderr.write(f"error: {exc}\n")
			return 1

		result = run_case(binary_path, case_path)
		output_text = result.stdout
		if result.stderr:
			output_text += ("\n" if output_text else "") + result.stderr
		out_path.write_text(output_text, encoding="utf-8")

		actual_success = result.returncode == 0
		mismatch = actual_success != expected_success
		if mismatch:
			status = f"{RED}verdict mismatch (expected {verdict_text}, exit {result.returncode}){RESET}"
		else:
			status = "ok" if expected_success else f"expected fail (exit {result.returncode})"
		print(f"[{index}/{len(cases)}] {rel_case} -> {out_path} : {status}")

		if mismatch:
			last_line = extract_last_line(output_text)
			mismatches.append((case_path, rel_case, verdict_text, expected_success, actual_success, result.returncode, last_line))

	summary_path = output_root / "summary"
	summary_lines: list[str] = []
	if mismatches:
		summary_lines.append(f"{len(mismatches)} verdict mismatch(s) out of {total_cases} cases:")
		for case_path, rel_case, verdict_text, expected_success, actual_success, returncode, last_line in mismatches:
			summary_lines.append(str(rel_case))
			summary_lines.append(f"  source: {case_path}")
			expected_label = "success" if expected_success else "failure"
			actual_label = "success" if actual_success else "failure"
			summary_lines.append(f"  expected: {verdict_text} ({expected_label})")
			summary_lines.append(f"  actual: exit {returncode} ({actual_label})")
			summary_lines.append(f"  last line: {last_line}")
			summary_lines.append("")
	else:
		summary_lines.append(f"No verdict mismatches detected. (0/{total_cases})")

	summary_path.write_text("\n".join(summary_lines).rstrip("\n") + "\n", encoding="utf-8")

	if mismatches:
		sys.stderr.write("\nVerdict mismatches:\n")
		for _, rel_case, verdict_text, _, actual_success, returncode, _ in mismatches:
			actual_label = "success" if actual_success else "failure"
			sys.stderr.write(f"{RED}- {rel_case} -> exit {returncode} ({actual_label}), expected {verdict_text}{RESET}\n")
		sys.stderr.write(f"{RED}Mismatch total: {len(mismatches)}/{total_cases}{RESET}\n")
		return 2

	print(f"Verdict mismatches: 0/{total_cases}")
	print(f"Verdict summary written to {summary_path}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())

