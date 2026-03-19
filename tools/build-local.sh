#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$(cd "$ROOT/.." && pwd)/.work"
QMK_UPSTREAM="https://github.com/qmk/qmk_firmware.git"
QMK_REF="master"
QMK_ACTION="compile"
CI_MODE="false"
CLEAN_BUILD="false"
KEYBOARD=""
KEYMAP=""
ARTIFACT_DIR=""
FLATCC_SRC_DIR="${FLATCC_SRC_DIR:-$WORK/vendor/flatcc}"

log() {
  printf '[build-local.sh] %s\n' "$*"
}

warn() {
  printf '[build-local.sh] warning: %s\n' "$*" >&2
}

die() {
  printf '[build-local.sh] error: %s\n' "$*" >&2
  exit 1
}

ensure_tool() {
  command -v "$1" >/dev/null 2>&1 || die "required tool not found: $1"
}

is_qmk_checkout() {
  local dir="$1"
  [ -d "$dir/.git" ] \
    && [ -d "$dir/quantum" ] \
    && [ -f "$dir/requirements.txt" ] \
    && [ -f "$dir/requirements-dev.txt" ]
}

bootstrap_qmk_checkout() {
  if is_qmk_checkout "$QMK_DIR"; then
    return 0
  fi

  if [ -e "$QMK_DIR" ] && [ -n "$(find "$QMK_DIR" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null)" ]; then
    die "existing path is not a qmk_firmware checkout: $QMK_DIR; remove it and rerun to bootstrap a clean checkout under .work"
  fi

  ensure_tool qmk
  mkdir -p "$(dirname "$QMK_DIR")"
  log "bootstrapping qmk_firmware via qmk clone at $QMK_DIR"
  qmk clone qmk/qmk_firmware "$QMK_DIR"
}

configure_qmk_remote() {
  if git -C "$QMK_DIR" remote get-url origin >/dev/null 2>&1; then
    git -C "$QMK_DIR" remote set-url origin "$QMK_UPSTREAM"
  else
    git -C "$QMK_DIR" remote add origin "$QMK_UPSTREAM"
  fi
}

usage() {
  cat <<'EOF'
Usage: v2_qmk_fw/tools/build-local.sh [options]

Options:
  --ci
  --clean
  --keyboard <name>
  --keymap <name>
  --qmk-upstream <url>
  --qmk-ref <ref>
  --qmk-action <compile|flash>
  --artifact-dir <path>
  --flatcc-src-dir <path>
EOF
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --ci)
      CI_MODE="true"
      shift
      ;;
    --clean)
      CLEAN_BUILD="true"
      shift
      ;;
    --keyboard)
      KEYBOARD="$2"
      shift 2
      ;;
    --keymap)
      KEYMAP="$2"
      shift 2
      ;;
    --qmk-upstream)
      QMK_UPSTREAM="$2"
      shift 2
      ;;
    --qmk-ref)
      QMK_REF="$2"
      shift 2
      ;;
    --qmk-action)
      QMK_ACTION="$2"
      shift 2
      ;;
    --artifact-dir)
      ARTIFACT_DIR="$2"
      shift 2
      ;;
    --flatcc-src-dir)
      FLATCC_SRC_DIR="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

[ -n "$KEYBOARD" ] || die "KEYBOARD is required, e.g. cue2keys"
[ -n "$KEYMAP" ] || die "KEYMAP is required, e.g. default"

case "$QMK_ACTION" in
  compile | flash) ;;
  *)
    die "unsupported qmk action: $QMK_ACTION"
    ;;
esac

if [ "$QMK_ACTION" = "flash" ] && [ "$CI_MODE" = "true" ]; then
  die "qmk flash is not supported with --ci"
fi

QMK_DIR="$WORK/qmk_firmware"
TARGET_DIR="$QMK_DIR/keyboards/$KEYBOARD"
FLATCC_STAGE_DIR="$TARGET_DIR/flatcc"
OVERLAY_STAGE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cue2keys-qmk-overlay.XXXXXX")"

cleanup() {
  rm -rf "$OVERLAY_STAGE_DIR"
}

trap cleanup EXIT

ensure_tool git
ensure_tool qmk
ensure_tool rsync

bootstrap_qmk_checkout
is_qmk_checkout "$QMK_DIR" || die "qmk_firmware checkout is invalid: $QMK_DIR"

configure_qmk_remote
git -C "$QMK_DIR" fetch origin "$QMK_REF" --depth 1
git -C "$QMK_DIR" checkout FETCH_HEAD

# Keep QMK's object cache for the default local path, but preserve full-clean
# behavior for CI and explicit rebuilds.
if [ "$CI_MODE" = "true" ] || [ "$CLEAN_BUILD" = "true" ]; then
  git -C "$QMK_DIR" clean -fdx
else
  git -C "$QMK_DIR" clean -fdx -e .build/ -e "keyboards/$KEYBOARD/"
fi

git -C "$QMK_DIR" submodule sync --recursive
git -C "$QMK_DIR" submodule update --init --recursive --depth 1

mkdir -p "$TARGET_DIR"
mkdir -p "$FLATCC_STAGE_DIR/include" "$FLATCC_STAGE_DIR/src/runtime"

git -C "$ROOT" ls-files -z --cached --others --exclude-standard \
  | rsync -a --delete --from0 --files-from=- "$ROOT/" "$OVERLAY_STAGE_DIR/"

rsync -a --delete --exclude '/flatcc/' "$OVERLAY_STAGE_DIR"/ "$TARGET_DIR"/

rsync -a --delete "$FLATCC_SRC_DIR/include/flatcc"/ "$FLATCC_STAGE_DIR/include/flatcc"/
rsync -a --delete "$FLATCC_SRC_DIR/src/runtime"/ "$FLATCC_STAGE_DIR/src/runtime"/

if [ ! -f "$TARGET_DIR/generated/include/pendant_reader.h" ]; then
  die "missing generated schema header: $TARGET_DIR/generated/include/pendant_reader.h; run the workspace-level schema sync/build entrypoint before invoking this script"
fi

if [ ! -f "$FLATCC_STAGE_DIR/include/flatcc/flatcc_flatbuffers.h" ]; then
  die "missing flatcc runtime header: $FLATCC_STAGE_DIR/include/flatcc/flatcc_flatbuffers.h; rerun the workspace-level QMK build entrypoint or pass --flatcc-src-dir with a valid flatcc checkout"
fi

cd "$QMK_DIR"
if [ "$QMK_ACTION" = "flash" ]; then
  log "running qmk flash -kb $KEYBOARD -km $KEYMAP"
  qmk flash -kb "$KEYBOARD" -km "$KEYMAP"
else
  log "running qmk compile -kb $KEYBOARD -km $KEYMAP"
  qmk compile -kb "$KEYBOARD" -km "$KEYMAP"
fi

if [ -n "$ARTIFACT_DIR" ]; then
  mkdir -p "$ARTIFACT_DIR"
  find "$ARTIFACT_DIR" -type f -delete
  while IFS= read -r artifact; do
    cp "$artifact" "$ARTIFACT_DIR/"
  done < <(
    find "$QMK_DIR/.build" -maxdepth 1 -type f \
      \( -name '*.bin' -o -name '*.elf' -o -name '*.hex' -o -name '*.uf2' \) \
      | sort
  )
fi
