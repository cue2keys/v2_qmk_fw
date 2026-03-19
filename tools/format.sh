#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mode="staged"

usage() {
  cat <<'EOF'
Usage: ./tools/format.sh [--check|--all]
EOF
}

die() {
  printf '[format.sh] error: %s\n' "$*" >&2
  exit 1
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --check)
      mode="check"
      shift
      ;;
    --all)
      mode="all"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

cd "$ROOT"

declare -a c_files=()
declare -a python_files=()

collect_paths() {
  if [ "$mode" = "check" ] || [ "$mode" = "all" ]; then
    git ls-files
    return 0
  fi

  git diff --cached --name-only --diff-filter=ACMR
}

while IFS= read -r path; do
  case "$path" in
    generated/*|kle.json)
      continue
      ;;
    *.c|*.h)
      c_files+=("$path")
      ;;
    *.py)
      python_files+=("$path")
      ;;
  esac
done < <(collect_paths)

if [ "${#c_files[@]}" -eq 0 ] && [ "${#python_files[@]}" -eq 0 ]; then
  exit 0
fi

if [ "${#c_files[@]}" -gt 0 ]; then
  command -v clang-format >/dev/null 2>&1 || die "required tool not found: clang-format"
  if [ "$mode" = "check" ]; then
    clang-format --dry-run -Werror --style=file "${c_files[@]}"
  else
    clang-format -i --style=file "${c_files[@]}"
    if [ "$mode" = "staged" ]; then
      git add -- "${c_files[@]}"
    fi
  fi
fi

if [ "${#python_files[@]}" -gt 0 ]; then
  command -v ruff >/dev/null 2>&1 || die "required tool not found: ruff"
  if [ "$mode" = "check" ]; then
    ruff format --check "${python_files[@]}"
  else
    ruff format "${python_files[@]}"
    if [ "$mode" = "staged" ]; then
      git add -- "${python_files[@]}"
    fi
  fi
fi
