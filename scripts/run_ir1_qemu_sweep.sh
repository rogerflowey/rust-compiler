#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: scripts/run_ir1_qemu_sweep.sh [--out-dir DIR] [--compiler PATH] [--cc PATH] [--qemu PATH] [--sysroot DIR] [--no-static] [--allow-compile-fail CASE]

Compile all `external/RCompiler-Testcases/IR-1/src/*/*.rx` cases through the
submission interface, link stdout `test.s` plus stderr `builtin.s` into RV64
Linux ELF executables, run them under qemu-riscv64, and compare stdout with
testcase `*.out` files. Per-case artifacts and a summary are written under
the output directory.

Options:
  --out-dir DIR            Output directory for artifacts and summary
  --compiler PATH          Submission compiler binary to run
  --pipeline PATH          Alias for --compiler
  --cc PATH                RV64 Linux compiler, default riscv64-linux-gnu-gcc
  --qemu PATH              QEMU user-mode binary, default qemu-riscv64
  --march VALUE            GCC -march value, default rv64gc
  --mabi VALUE             GCC -mabi value, default lp64d
  --sysroot DIR            Pass -L DIR to qemu-riscv64
  --no-static              Link dynamically instead of passing -static
  --allow-compile-fail X   Case name allowed to fail compilation; repeatable
  -h, --help               Show this help
EOF
}

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)

default_compiler() {
    if [[ -x "$repo_root/build/cmd/submission_pipeline" ]]; then
        printf '%s\n' "$repo_root/build/cmd/submission_pipeline"
    else
        printf '%s\n' "$repo_root/build/ninja-debug/cmd/submission_pipeline"
    fi
}

is_executable_command() {
    local cmd="$1"
    if [[ "$cmd" == */* ]]; then
        [[ -x "$cmd" ]]
    else
        command -v "$cmd" >/dev/null 2>&1
    fi
}

out_dir=""
compiler=$(default_compiler)
cc="riscv64-linux-gnu-gcc"
qemu="qemu-riscv64"
march="rv64gc"
mabi="lp64d"
sysroot=""
static_link=1
declare -a allow_compile_fail=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --out-dir)
            out_dir="${2:?missing value for --out-dir}"
            shift 2
            ;;
        --compiler|--pipeline)
            compiler="${2:?missing value for $1}"
            shift 2
            ;;
        --cc)
            cc="${2:?missing value for --cc}"
            shift 2
            ;;
        --qemu)
            qemu="${2:?missing value for --qemu}"
            shift 2
            ;;
        --march)
            march="${2:?missing value for --march}"
            shift 2
            ;;
        --mabi)
            mabi="${2:?missing value for --mabi}"
            shift 2
            ;;
        --sysroot)
            sysroot="${2:?missing value for --sysroot}"
            shift 2
            ;;
        --no-static)
            static_link=0
            shift
            ;;
        --allow-compile-fail)
            allow_compile_fail+=("${2:?missing value for --allow-compile-fail}")
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [[ -z "$out_dir" ]]; then
    out_dir="$repo_root/external/test-results/ir1-qemu-runtime-$(date +%F-%H%M%S)"
fi

if [[ ! -x "$compiler" ]]; then
    echo "Submission compiler not found or not executable: $compiler" >&2
    exit 1
fi
if ! is_executable_command "$cc"; then
    echo "RV64 compiler not found or not executable: $cc" >&2
    exit 1
fi
if ! is_executable_command "$qemu"; then
    echo "QEMU binary not found or not executable: $qemu" >&2
    exit 1
fi
if [[ -n "$sysroot" && ! -d "$sysroot" ]]; then
    echo "QEMU sysroot is not a directory: $sysroot" >&2
    exit 1
fi

case_root="$repo_root/external/RCompiler-Testcases/IR-1/src"
if [[ ! -d "$case_root" ]]; then
    echo "Missing testcase corpus: $case_root" >&2
    exit 1
fi

mkdir -p "$out_dir"

is_allowed_compile_fail() {
    local name="$1"
    local allowed
    for allowed in "${allow_compile_fail[@]}"; do
        if [[ "$allowed" == "$name" ]]; then
            return 0
        fi
    done
    return 1
}

normalized_diff() {
    local lhs="$1"
    local rhs="$2"
    diff -u \
        <(perl -0pe 's/\n*\z/\n/' "$lhs") \
        <(perl -0pe 's/\n*\z/\n/' "$rhs")
}

link_case() {
    local case_out_dir="$1"
    local -a cc_args=("-march=$march" "-mabi=$mabi")
    if [[ "$static_link" -eq 1 ]]; then
        cc_args+=("-static")
    fi
    cc_args+=("test.s" "builtin.s" "-o" "test.elf")
    (cd "$case_out_dir" && "$cc" "${cc_args[@]}" >link.stdout 2>link.stderr)
}

run_case() {
    local case_out_dir="$1"
    local -a qemu_args=()
    if [[ -n "$sysroot" ]]; then
        qemu_args+=("-L" "$sysroot")
    fi
    qemu_args+=("./test.elf")
    (cd "$case_out_dir" && "$qemu" "${qemu_args[@]}" <test.in >actual.out 2>qemu.stderr)
}

pass=0
expected_compile_fail=0
unexpected_compile_fail=0
link_fail=0
runtime_fail=0
total=0

while IFS= read -r rx; do
    total=$((total + 1))
    case_dir=$(dirname "$rx")
    name=$(basename "${rx%.rx}")
    case_out_dir="$out_dir/$name"
    mkdir -p "$case_out_dir"

    if "$compiler" <"$rx" >"$case_out_dir/test.s" 2>"$case_out_dir/builtin.s"; then
        :
    elif is_allowed_compile_fail "$name"; then
        expected_compile_fail=$((expected_compile_fail + 1))
        printf 'EXPECTED_COMPILE_FAIL %s\n' "$name" | tee "$case_out_dir/status.txt"
        continue
    else
        unexpected_compile_fail=$((unexpected_compile_fail + 1))
        printf 'UNEXPECTED_COMPILE_FAIL %s\n' "$name" | tee "$case_out_dir/status.txt"
        continue
    fi

    cp "$case_dir/$name.in" "$case_out_dir/test.in"
    cp "$case_dir/$name.out" "$case_out_dir/expected.out"

    if ! link_case "$case_out_dir"; then
        link_fail=$((link_fail + 1))
        printf 'LINK_FAIL %s\n' "$name" | tee "$case_out_dir/status.txt"
        continue
    fi

    if run_case "$case_out_dir"; then
        if normalized_diff "$case_out_dir/expected.out" "$case_out_dir/actual.out" >"$case_out_dir/diff.txt"; then
            pass=$((pass + 1))
            printf 'PASS %s\n' "$name" | tee "$case_out_dir/status.txt"
        else
            runtime_fail=$((runtime_fail + 1))
            printf 'RUNTIME_OUTPUT_FAIL %s\n' "$name" | tee "$case_out_dir/status.txt"
        fi
    else
        runtime_fail=$((runtime_fail + 1))
        printf 'RUNTIME_EXEC_FAIL %s\n' "$name" | tee "$case_out_dir/status.txt"
    fi
done < <(find "$case_root" -mindepth 2 -maxdepth 2 -name '*.rx' | sort)

summary="$out_dir/README.md"
{
    printf 'IR-1 QEMU runtime sweep\n\n'
    printf -- '- Corpus: `%s`\n' "${case_root#$repo_root/}"
    printf -- '- Driver: `%s < source.rx > test.s 2> builtin.s` + `%s -march=%s -mabi=%s%s test.s builtin.s` + `%s%s`\n' \
        "${compiler#$repo_root/}" "$cc" "$march" "$mabi" \
        "$([[ "$static_link" -eq 1 ]] && printf ' -static')" \
        "$qemu" "$([[ -n "$sysroot" ]] && printf ' -L %s' "$sysroot")"
    printf -- '- Total cases: `%d`\n' "$total"
    printf -- '- Passed end-to-end: `%d`\n' "$pass"
    printf -- '- Expected compile failures: `%d`\n' "$expected_compile_fail"
    printf -- '- Unexpected compile failures: `%d`\n' "$unexpected_compile_fail"
    printf -- '- Link failures: `%d`\n' "$link_fail"
    printf -- '- Runtime or output failures: `%d`\n\n' "$runtime_fail"

    if [[ ${#allow_compile_fail[@]} -gt 0 ]]; then
        printf 'Allowed compile failures:\n\n'
        local_name=""
        for local_name in "${allow_compile_fail[@]}"; do
            printf -- '- `%s`\n' "$local_name"
        done
        printf '\n'
    fi

    printf 'Non-passing cases:\n\n'
    find "$out_dir" -mindepth 2 -maxdepth 2 -name status.txt -print0 | sort -z | while IFS= read -r -d '' status_file; do
        status=$(cat "$status_file")
        case_name=$(basename "$(dirname "$status_file")")
        if [[ "$status" != PASS* ]]; then
            printf -- '- `%s`: `%s`\n' "$case_name" "$status"
        fi
    done
} >"$summary"

cat "$summary"

if [[ "$unexpected_compile_fail" -ne 0 || "$link_fail" -ne 0 || "$runtime_fail" -ne 0 ]]; then
    exit 1
fi
