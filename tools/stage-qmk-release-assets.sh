#!/usr/bin/env bash
set -euo pipefail

SCRIPT_NAME="stage-qmk-release-assets.sh"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/tools/lib/common.sh"

KEYBOARD="cue2keys"
KEYMAP="default"
REPO_SLUG="${REPO_SLUG:-cue2keys/v2_qmk_fw}"
ARTIFACTS_DIR=""
RELEASE_DIR="${RELEASE_DIR:-$ROOT/.out/qmk-release}"
RELEASE_TAG=""
RELEASE_URL=""
TIMESTAMP=""
GITHUB_OUTPUT_PATH="${GITHUB_OUTPUT:-}"

usage() {
  cat <<'EOF'
Usage: v2_qmk_fw/tools/stage-qmk-release-assets.sh [options]

Options:
  --keyboard <name>
  --keymap <name>
  --repo <owner/name>
  --artifact-dir <path>
  --release-dir <path>
  --release-tag <value>
  --release-url <url>
  --timestamp <value>
  --github-output <path>
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --keyboard)
      KEYBOARD="$2"
      shift 2
      ;;
    --keymap)
      KEYMAP="$2"
      shift 2
      ;;
    --repo)
      REPO_SLUG="$2"
      shift 2
      ;;
    --artifact-dir)
      ARTIFACTS_DIR="$2"
      shift 2
      ;;
    --release-dir)
      RELEASE_DIR="$2"
      shift 2
      ;;
    --release-tag)
      RELEASE_TAG="$2"
      shift 2
      ;;
    --release-url)
      RELEASE_URL="$2"
      shift 2
      ;;
    --timestamp)
      TIMESTAMP="$2"
      shift 2
      ;;
    --github-output)
      GITHUB_OUTPUT_PATH="$2"
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

resolve_path() {
  case "$1" in
    /*) printf '%s\n' "$1" ;;
    *) printf '%s\n' "$ROOT/$1" ;;
  esac
}

[ -n "$ARTIFACTS_DIR" ] || die "--artifact-dir is required"
[ -n "$RELEASE_TAG" ] || die "--release-tag is required"
[ -n "$RELEASE_URL" ] || die "--release-url is required"
[ -n "$TIMESTAMP" ] || TIMESTAMP="$(iso_utc_now)"

ARTIFACTS_DIR="$(resolve_path "$ARTIFACTS_DIR")"
RELEASE_DIR="$(resolve_path "$RELEASE_DIR")"
[ -d "$ARTIFACTS_DIR" ] || die "artifact dir does not exist: $ARTIFACTS_DIR"

fw_version="$(read_qmk_fw_version "$ROOT/config.h")"
uf2_name="${KEYBOARD}_${KEYMAP}_${fw_version}.uf2"
manifest_name="manifest.json"

uf2_candidates=()
while IFS= read -r candidate; do
  uf2_candidates+=("$candidate")
done < <(find "$ARTIFACTS_DIR" -type f -name '*.uf2' | sort)
[ "${#uf2_candidates[@]}" -eq 1 ] || die "expected exactly one uf2 artifact in $ARTIFACTS_DIR, found ${#uf2_candidates[@]}"

ensure_dir "$RELEASE_DIR"
find "$RELEASE_DIR" -mindepth 1 -maxdepth 1 -type f -delete
cp "${uf2_candidates[0]}" "$RELEASE_DIR/$uf2_name"

"$ROOT/tools/generate-release-manifest.sh" \
  --artifact-dir "$RELEASE_DIR" \
  --output "$RELEASE_DIR/$manifest_name" \
  --keyboard "$KEYBOARD" \
  --keymap "$KEYMAP" \
  --repo "$REPO_SLUG" \
  --release-tag "$RELEASE_TAG" \
  --release-url "$RELEASE_URL" \
  --timestamp "$TIMESTAMP"

if [ -n "$GITHUB_OUTPUT_PATH" ]; then
  {
    printf 'fw_version=%s\n' "$fw_version"
    printf 'release_tag=%s\n' "$RELEASE_TAG"
    printf 'release_url=%s\n' "$RELEASE_URL"
    printf 'uf2_name=%s\n' "$uf2_name"
    printf 'manifest_name=%s\n' "$manifest_name"
  } >>"$GITHUB_OUTPUT_PATH"
fi

log "release assets staged in $RELEASE_DIR from $ARTIFACTS_DIR"
