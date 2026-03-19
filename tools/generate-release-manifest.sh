#!/usr/bin/env bash
set -euo pipefail

SCRIPT_NAME="generate-release-manifest.sh"
source "$(cd "$(dirname "$0")" && pwd)/lib/common.sh"

root="$(cd "$(dirname "$0")/.." && pwd)"
artifact_dir=""
output=""
keyboard="cue2keys"
keymap="default"
repo_slug="cue2keys/v2_qmk_fw"
release_tag=""
release_url=""
timestamp=""

usage() {
  cat <<'EOF'
Usage: v2_qmk_fw/tools/generate-release-manifest.sh [options]

Options:
  --artifact-dir <path>
  --output <path>
  --keyboard <name>
  --keymap <name>
  --repo <owner/name>
  --release-tag <value>
  --release-url <value>
  --timestamp <value>
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --artifact-dir)
      artifact_dir="$2"
      shift 2
      ;;
    --output)
      output="$2"
      shift 2
      ;;
    --keyboard)
      keyboard="$2"
      shift 2
      ;;
    --keymap)
      keymap="$2"
      shift 2
      ;;
    --repo)
      repo_slug="$2"
      shift 2
      ;;
    --release-tag)
      release_tag="$2"
      shift 2
      ;;
    --release-url)
      release_url="$2"
      shift 2
      ;;
    --timestamp)
      timestamp="$2"
      shift 2
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

[ -n "$artifact_dir" ] || die "--artifact-dir is required"
[ -d "$artifact_dir" ] || die "artifact dir does not exist: $artifact_dir"
[ -n "$release_tag" ] || die "--release-tag is required"
[ -n "$release_url" ] || die "--release-url is required"
[ -n "$timestamp" ] || timestamp="$(iso_utc_now)"

if [ -z "$output" ]; then
  output="$artifact_dir/manifest.json"
fi

fw_version="$(read_qmk_fw_version "$root/config.h")"

python3 - "$root" "$artifact_dir" "$output" "$keyboard" "$keymap" "$repo_slug" "$release_tag" "$release_url" "$timestamp" "$fw_version" <<'PY'
import hashlib
import json
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
artifact_dir = Path(sys.argv[2])
output_path = Path(sys.argv[3])
keyboard = sys.argv[4]
keymap = sys.argv[5]
repo_slug = sys.argv[6]
release_tag = sys.argv[7]
release_url = sys.argv[8]
timestamp = sys.argv[9]
fw_version = sys.argv[10]


def git_value(repo: Path, *args: str) -> str:
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()


def dirty(repo: Path) -> bool:
    return bool(git_value(repo, "status", "--porcelain", "--ignore-submodules=none"))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


artifacts = []
for candidate in sorted(path for path in artifact_dir.iterdir() if path.is_file()):
    if candidate.resolve() == output_path.resolve():
        continue
    artifacts.append(
        {
            "name": candidate.name,
            "path": candidate.name,
            "size": candidate.stat().st_size,
            "sha256": sha256(candidate),
        }
    )

uf2_artifacts = [artifact for artifact in artifacts if artifact["name"].endswith(".uf2")]
if len(uf2_artifacts) != 1:
    raise SystemExit(f"release manifest requires exactly one uf2 artifact, found {len(uf2_artifacts)}")

uf2_artifact = uf2_artifacts[0]

manifest = {
    "manifest_version": 1,
    "release": {
        "mode": "release",
        "version": release_tag,
        "timestamp": timestamp,
    },
    "inputs": {
        "v2_qmk_fw": {
            "commit": git_value(root, "rev-parse", "HEAD"),
            "dirty": dirty(root),
        }
    },
    "artifacts": artifacts,
    "qmk": {
        "firmware_version": fw_version,
        "keyboard": keyboard,
        "keymap": keymap,
        "release_tag": release_tag,
        "release_url": release_url,
        "repository": repo_slug,
        "uf2": {
            "name": uf2_artifact["name"],
            "path": uf2_artifact["path"],
            "sha256": uf2_artifact["sha256"],
        },
    },
}

output_path.parent.mkdir(parents=True, exist_ok=True)
output_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="ascii")
PY

log "manifest written to $output"
