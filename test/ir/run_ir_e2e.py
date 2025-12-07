#!/usr/bin/env python3
"""Run end-to-end IR pipeline tests using fixtures in test/ir.

This mirrors scripts/test_ir_pipeline.py but defaults to the repo-local
fixtures so it can be used from CI or developers without reaching into
RCompiler-Testcases.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

CLANG_CANDIDATES = ["clang-18", "clang-17", "clang-16", "clang-15", "clang"]
RED = "\033[31m"
GREEN = "\033[32m"
RESET = "\033[0m"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run ir_pipeline on test/ir fixtures, assemble, execute with reimu, and compare outputs.",
    )
    parser.add_argument(
        "--binary",
        default="build/ninja-debug/cmd/ir_pipeline",
        help="Path to ir_pipeline binary relative to the repository root.",
    )
    parser.add_argument(
        "--test-root",
        default="test/ir",
        help="Directory containing IR fixtures relative to the repository root.",
    )
    parser.add_argument(
        "--builtin",
        help="Path to builtin.c; defaults to scripts/builtin.c.",
    )
    parser.add_argument(
        "--target",
        default="riscv32-unknown-elf",
        help="Target triple passed to clang for assembly generation.",
    )
    parser.add_argument(
        "--clang",
        help="Clang executable to use. If omitted, the script auto-detects a suitable clang.* binary.",
    )
    parser.add_argument(
        "--reimu",
        default="reimu",
        help="Reimu executable to run the generated assembly.",
    )
    parser.add_argument(
        "--output-root",
        help="Directory to store outputs and logs; default is <test-root>/output",
    )
    parser.add_argument(
        "--keep-working-files",
        action="store_true",
        help="Keep intermediate working files (builtin.s, test.ll, test.s, etc.) in output directory.",
    )
    return parser.parse_args()


def detect_clang(user_choice: str | None) -> str:
    if user_choice:
        if shutil.which(user_choice):
            return user_choice
        sys.stderr.write(f"error: clang executable not found: {user_choice}\n")
        sys.exit(1)

    for candidate in CLANG_CANDIDATES:
        if shutil.which(candidate):
            return candidate

    sys.stderr.write("error: no suitable clang executable found (tried clang-18, clang-17, clang-16, clang-15, clang)\n")
    sys.exit(1)


def detect_reimu(user_choice: str) -> str:
    if shutil.which(user_choice):
        return user_choice
    sys.stderr.write(f"error: reimu executable not found: {user_choice}\n")
    sys.exit(1)


def run_cmd(cmd: list[str], cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)


def ensure_file(path: Path, description: str) -> None:
    if not path.is_file():
        sys.stderr.write(f"error: {description} not found: {path}\n")
        sys.exit(1)


def ensure_dir(path: Path, description: str) -> None:
    if not path.is_dir():
        sys.stderr.write(f"error: {description} not found: {path}\n")
        sys.exit(1)


def remove_plt(source: Path, dest: Path) -> None:
    dest.write_text(source.read_text(encoding="utf-8").replace("@plt", ""), encoding="utf-8")


def normalize_output(path: Path) -> list[str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    return [line.rstrip() for line in lines if line.rstrip()]


def compile_builtin(clang: str, target: str, builtin_path: Path, work_root: Path) -> Path:
    builtin_source = work_root / "builtin.s.source"
    builtin_clean = work_root / "builtin.s"

    result = run_cmd(
        [clang, "-S", f"--target={target}", "-O2", "-fno-builtin", str(builtin_path), "-o", str(builtin_source)],
    )
    if result.returncode != 0:
        sys.stderr.write("error: failed to compile builtin.c\n")
        sys.stderr.write(result.stderr)
        sys.exit(1)

    remove_plt(builtin_source, builtin_clean)
    return builtin_clean


def extract_last_line(text: str) -> str:
    stripped = text.rstrip().splitlines()
    return stripped[-1] if stripped else "<no output>"


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[2]

    clang = detect_clang(args.clang)
    reimu = detect_reimu(args.reimu)

    binary_path = (repo_root / args.binary).resolve()
    ensure_file(binary_path, "ir_pipeline binary")

    test_root = (repo_root / args.test_root).resolve()
    src_dir = test_root / "src"
    ensure_dir(src_dir, "src directory")

    if args.builtin:
        builtin_path = Path(args.builtin)
    else:
        builtin_path = repo_root / "scripts" / "builtin.c"
    builtin_path = builtin_path.resolve()
    ensure_file(builtin_path, "builtin.c")

    output_root = Path(args.output_root) if args.output_root else (test_root / "output")
    output_root = output_root.resolve()
    output_root.mkdir(parents=True, exist_ok=True)

    cases = sorted(src_dir.rglob("*.rx"))
    if not cases:
        sys.stderr.write(f"warning: no .rx files found under {src_dir}\n")
        return 0

    print(f"Using clang='{clang}', reimu='{reimu}', target='{args.target}'")
    print(f"Output directory: {output_root}")

    compiled_builtin = compile_builtin(clang, args.target, builtin_path, output_root)

    failures: list[tuple[Path, str]] = []
    total = len(cases)

    for idx, case_path in enumerate(cases, start=1):
        rel_case = case_path.relative_to(src_dir)
        case_dir = case_path.parent
        case_name = case_path.stem
        case_in = case_dir / f"{case_name}.in"
        case_out = case_dir / f"{case_name}.out"

        if not case_in.is_file() or not case_out.is_file():
            failures.append((rel_case, "missing .in or .out file"))
            print(f"[{idx}/{total}] {rel_case}: {RED}missing input/output{RESET}")
            continue

        work_dir = output_root / rel_case.parent
        work_dir.mkdir(parents=True, exist_ok=True)

        shutil.copy(compiled_builtin, work_dir / "builtin.s")
        shutil.copy(case_in, work_dir / "test.in")
        shutil.copy(case_out, work_dir / "test.ans")

        ir_path = work_dir / "test.ll"
        asm_source = work_dir / "test.s.source"
        asm_clean = work_dir / "test.s"
        actual_output = work_dir / "test.out"

        log_lines: list[str] = []

        result_ir = run_cmd([str(binary_path), str(case_path), str(ir_path)])
        log_lines.append("== ir_pipeline ==")
        if result_ir.stdout:
            log_lines.append(result_ir.stdout.rstrip())
        if result_ir.stderr:
            log_lines.append(result_ir.stderr.rstrip())

        if result_ir.returncode != 0:
            reason = f"ir_pipeline exit {result_ir.returncode}: {extract_last_line(result_ir.stderr or result_ir.stdout)}"
            failures.append((rel_case, reason))
            print(f"[{idx}/{total}] {rel_case}: {RED}fail (compile){RESET}")
            (output_root / rel_case.parent).mkdir(parents=True, exist_ok=True)
            (output_root / rel_case.with_suffix(".log")).write_text("\n".join(log_lines).rstrip() + "\n", encoding="utf-8")
            continue

        result_clang = run_cmd([clang, "-S", f"--target={args.target}", str(ir_path), "-o", str(asm_source)])
        log_lines.append("== clang ==")
        if result_clang.stdout:
            log_lines.append(result_clang.stdout.rstrip())
        if result_clang.stderr:
            log_lines.append(result_clang.stderr.rstrip())

        if result_clang.returncode != 0:
            reason = f"clang exit {result_clang.returncode}: {extract_last_line(result_clang.stderr or result_clang.stdout)}"
            failures.append((rel_case, reason))
            print(f"[{idx}/{total}] {rel_case}: {RED}fail (clang){RESET}")
            (output_root / rel_case.parent).mkdir(parents=True, exist_ok=True)
            (output_root / rel_case.with_suffix(".log")).write_text("\n".join(log_lines).rstrip() + "\n", encoding="utf-8")
            continue

        remove_plt(asm_source, asm_clean)

        result_run = run_cmd([
            reimu,
            f"-i={work_dir / 'test.in'}",
            f"-o={actual_output}",
        ], cwd=work_dir)
        log_lines.append("== reimu ==")
        if result_run.stdout:
            log_lines.append(result_run.stdout.rstrip())
        if result_run.stderr:
            log_lines.append(result_run.stderr.rstrip())

        if result_run.returncode != 0:
            reason = f"reimu exit {result_run.returncode}: {extract_last_line(result_run.stderr or result_run.stdout)}"
            failures.append((rel_case, reason))
            print(f"[{idx}/{total}] {rel_case}: {RED}fail (runtime){RESET}")
            (output_root / rel_case.parent).mkdir(parents=True, exist_ok=True)
            (output_root / rel_case.with_suffix(".log")).write_text("\n".join(log_lines).rstrip() + "\n", encoding="utf-8")
            continue

        actual_lines = normalize_output(actual_output)
        expected_lines = normalize_output(work_dir / "test.ans")
        matched = actual_lines == expected_lines

        (output_root / rel_case.parent).mkdir(parents=True, exist_ok=True)
        (output_root / rel_case.with_suffix(".out")).write_text(actual_output.read_text(encoding="utf-8"), encoding="utf-8")
        (output_root / rel_case.with_suffix(".log")).write_text("\n".join(log_lines).rstrip() + "\n", encoding="utf-8")

        if matched:
            print(f"[{idx}/{total}] {rel_case}: {GREEN}pass{RESET}")
        else:
            failures.append((rel_case, f"output mismatch"))
            print(f"[{idx}/{total}] {rel_case}: {RED}fail (output){RESET}")

    summary_path = output_root / "summary"
    summary_lines = [f"Total: {total}, Passed: {total - len(failures)}, Failed: {len(failures)}"]
    if failures:
        summary_lines.append("")
        summary_lines.append("Failures:")
        for rel_case, reason in failures:
            summary_lines.append(f"- {rel_case}: {reason}")
    else:
        summary_lines.append("All cases succeeded.")

    summary_path.write_text("\n".join(summary_lines).rstrip() + "\n", encoding="utf-8")
    print(f"Summary written to {summary_path}")

    if failures:
        for rel_case, reason in failures:
            sys.stderr.write(f"{RED}- {rel_case}: {reason}{RESET}\n")
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
