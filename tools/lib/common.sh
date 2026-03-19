#!/usr/bin/env bash

SCRIPT_NAME="${SCRIPT_NAME:-$(basename "$0")}"

log() {
  printf '[%s] %s\n' "$SCRIPT_NAME" "$*"
}

die() {
  printf '[%s] error: %s\n' "$SCRIPT_NAME" "$*" >&2
  exit 1
}

ensure_dir() {
  mkdir -p "$1"
}

iso_utc_now() {
  date -u +"%Y-%m-%dT%H:%M:%SZ"
}

compact_utc_now() {
  date -u +"%Y%m%dT%H%M%SZ"
}

is_repo_dirty() {
  local repo="$1"
  [ -n "$(git -C "$repo" status --porcelain --ignore-submodules=none)" ]
}

read_qmk_fw_version() {
  local config_path="$1"

  [ -f "$config_path" ] || die "qmk config not found: $config_path"

  python3 - "$config_path" <<'PY'
import re
import sys
from pathlib import Path

config_path = Path(sys.argv[1])
pattern = re.compile(r'^\s*#define\s+FW_VERSION\s+"([^"]+)"(?:\s*//.*)?\s*$')

for line in config_path.read_text(encoding="utf-8").splitlines():
    match = pattern.match(line)
    if match:
        print(match.group(1))
        raise SystemExit(0)

raise SystemExit(f"FW_VERSION not found in {config_path}")
PY
}
