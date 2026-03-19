#!/usr/bin/env bash
set -euo pipefail

SCRIPT_NAME="run-qmk-firmware-release.sh"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORKSPACE_ROOT="$(cd "$ROOT/.." && pwd)"
source "$ROOT/tools/lib/common.sh"

KEYBOARD="cue2keys"
KEYMAP="default"
REPO_SLUG="${REPO_SLUG:-cue2keys/v2_qmk_fw}"
ARTIFACTS_DIR="${ARTIFACTS_DIR:-$ROOT/.out/qmk-build}"
RELEASE_DIR="${RELEASE_DIR:-$ROOT/.out/qmk-release}"
FLATCC_DIR="${FLATCC_DIR:-$WORKSPACE_ROOT/.work/vendor/flatcc}"
GITHUB_OUTPUT_PATH="${GITHUB_OUTPUT:-}"
FW_VERSION_OVERRIDE=""
RELEASE_TAG=""
RELEASE_URL=""
TIMESTAMP=""
DRY_RUN="false"

usage() {
  cat <<'EOF'
Usage: v2_qmk_fw/tools/run-qmk-firmware-release.sh [options]

Options:
  --keyboard <name>
  --keymap <name>
  --repo <owner/name>
  --artifacts-dir <path>
  --release-dir <path>
  --flatcc-dir <path>
  --fw-version <x.y.z>
  --release-tag <value>
  --release-url <url>
  --timestamp <value>
  --github-output <path>
  --dry-run
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
    --artifacts-dir)
      ARTIFACTS_DIR="$2"
      shift 2
      ;;
    --release-dir)
      RELEASE_DIR="$2"
      shift 2
      ;;
    --flatcc-dir)
      FLATCC_DIR="$2"
      shift 2
      ;;
    --fw-version)
      FW_VERSION_OVERRIDE="$2"
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
    --dry-run)
      DRY_RUN="true"
      shift
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

ARTIFACTS_DIR="$(resolve_path "$ARTIFACTS_DIR")"
RELEASE_DIR="$(resolve_path "$RELEASE_DIR")"
case "$FLATCC_DIR" in
  /*) ;;
  *) FLATCC_DIR="$WORKSPACE_ROOT/$FLATCC_DIR" ;;
esac
[ -n "$TIMESTAMP" ] || TIMESTAMP="$(compact_utc_now)"

[ -x "$ROOT/tools/build-local.sh" ] || die "missing qmk build entrypoint: $ROOT/tools/build-local.sh"
[ -x "$ROOT/tools/generate-release-manifest.sh" ] || die "missing manifest entrypoint: $ROOT/tools/generate-release-manifest.sh"
[ -f "$ROOT/config.h" ] || die "missing qmk config: $ROOT/config.h"
[ -f "$ROOT/generated/include/pendant_reader.h" ] || die "missing generated schema header: $ROOT/generated/include/pendant_reader.h"

if [ ! -d "$FLATCC_DIR/.git" ]; then
  if [ "$DRY_RUN" = "true" ]; then
    log "would clone flatcc into $FLATCC_DIR"
  else
    ensure_dir "$(dirname "$FLATCC_DIR")"
    log "cloning flatcc into $FLATCC_DIR"
    git clone --depth 1 https://github.com/dvidelabs/flatcc.git "$FLATCC_DIR"
  fi
fi

config_fw_version="$(read_qmk_fw_version "$ROOT/config.h")"
if [ -n "$FW_VERSION_OVERRIDE" ]; then
  [ "$FW_VERSION_OVERRIDE" = "$config_fw_version" ] \
    || die "FW version override ($FW_VERSION_OVERRIDE) does not match config.h ($config_fw_version)"
  fw_version="$FW_VERSION_OVERRIDE"
else
  fw_version="$config_fw_version"
fi

release_tag="${RELEASE_TAG:-v${fw_version}}"
release_url="${RELEASE_URL:-https://github.com/${REPO_SLUG}/releases/tag/${release_tag}}"

if [ "$DRY_RUN" = "true" ]; then
  log "qmk dir: $ROOT"
  log "flatcc dir: $FLATCC_DIR"
  log "artifacts dir: $ARTIFACTS_DIR"
  log "release dir: $RELEASE_DIR"
  log "fw version: $fw_version"
  log "release tag: $release_tag"
  log "would build qmk firmware and stage release assets"
  exit 0
fi

log "building flatcc in $FLATCC_DIR"
(
  cd "$FLATCC_DIR"
  ./scripts/initbuild.sh make
  ./scripts/build.sh
)

log "building qmk firmware"
"$ROOT/tools/build-local.sh" \
  --ci \
  --keyboard "$KEYBOARD" \
  --keymap "$KEYMAP" \
  --artifact-dir "$ARTIFACTS_DIR" \
  --flatcc-src-dir "$FLATCC_DIR"

"$ROOT/tools/stage-qmk-release-assets.sh" \
  --keyboard "$KEYBOARD" \
  --keymap "$KEYMAP" \
  --repo "$REPO_SLUG" \
  --artifact-dir "$ARTIFACTS_DIR" \
  --release-dir "$RELEASE_DIR" \
  --release-tag "$release_tag" \
  --release-url "$release_url" \
  --timestamp "$(iso_utc_now)" \
  --github-output "$GITHUB_OUTPUT_PATH"
