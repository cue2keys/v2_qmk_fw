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
QMK_PROFILE="debug"
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

detect_qmk_parallel() {
  if [ -n "${QMK_PARALLEL:-}" ]; then
    printf '%s\n' "$QMK_PARALLEL"
    return 0
  fi

  local jobs=""

  if command -v getconf >/dev/null 2>&1; then
    jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)"
  fi

  if [ -z "$jobs" ] && command -v sysctl >/dev/null 2>&1; then
    jobs="$(sysctl -n hw.logicalcpu 2>/dev/null || true)"
  fi

  if [ -z "$jobs" ] && command -v nproc >/dev/null 2>&1; then
    jobs="$(nproc 2>/dev/null || true)"
  fi

  if [ -n "$jobs" ] && [ "$jobs" -ge 1 ] 2>/dev/null; then
    printf '%s\n' "$jobs"
  else
    printf '1\n'
  fi
}

select_qmk_make() {
  if [ -n "${MAKE:-}" ] && command -v "$MAKE" >/dev/null 2>&1; then
    printf '%s\n' "$MAKE"
    return 0
  fi

  if command -v gmake >/dev/null 2>&1; then
    printf 'gmake\n'
    return 0
  fi

  printf 'make\n'
}

make_supports_output_sync() {
  local make_cmd="$1"
  "$make_cmd" --help 2>/dev/null | grep -q -- '--output-sync'
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
  --qmk-profile <debug|release>
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
    --qmk-profile)
      QMK_PROFILE="$2"
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

case "$QMK_PROFILE" in
  debug | release) ;;
  *)
    die "unsupported qmk profile: $QMK_PROFILE"
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

profile_env_args=()
if [ "$QMK_PROFILE" = "release" ]; then
  release_console_enable="${QMK_RELEASE_CONSOLE_ENABLE-no}"
  release_command_enable="${QMK_RELEASE_COMMAND_ENABLE-no}"
  release_lto_enable="${QMK_RELEASE_LTO_ENABLE-yes}"
  release_opt="${QMK_RELEASE_OPT-2}"
  release_extraflags="${QMK_RELEASE_EXTRAFLAGS--flto=auto}"

  profile_env_args=(
    -e "CONSOLE_ENABLE=${release_console_enable}"
    -e "COMMAND_ENABLE=${release_command_enable}"
  )
  if [ -n "$release_lto_enable" ]; then
    profile_env_args+=(-e "LTO_ENABLE=${release_lto_enable}")
  fi
  if [ -n "$release_opt" ]; then
    profile_env_args+=(-e "OPT=${release_opt}")
  fi
  if [ -n "$release_extraflags" ]; then
    profile_env_args+=(-e "EXTRAFLAGS=${release_extraflags}")
  fi
fi

qmk_parallel="$(detect_qmk_parallel)"
qmk_make="$(select_qmk_make)"
use_qmk_parallel="false"
if [ "$qmk_parallel" -gt 1 ] 2>/dev/null && make_supports_output_sync "$qmk_make"; then
  use_qmk_parallel="true"
fi

cd "$QMK_DIR"
if [ "$QMK_ACTION" = "flash" ]; then
  log "running qmk flash -kb $KEYBOARD -km $KEYMAP (profile: $QMK_PROFILE)"
  qmk_cmd=(qmk flash)
else
  log "running qmk compile -kb $KEYBOARD -km $KEYMAP (profile: $QMK_PROFILE)"
  qmk_cmd=(qmk compile)
fi

if [ "$QMK_PROFILE" = "release" ] || [ "${#profile_env_args[@]}" -gt 0 ]; then
  qmk_cmd+=("${profile_env_args[@]}")
fi

if [ "$use_qmk_parallel" = "true" ]; then
  qmk_cmd+=(-j "$qmk_parallel")
fi

qmk_cmd+=(-kb "$KEYBOARD" -km "$KEYMAP")
MAKE="$qmk_make" "${qmk_cmd[@]}"

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
