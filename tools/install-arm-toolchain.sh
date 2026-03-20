#!/usr/bin/env bash
set -euo pipefail

SCRIPT_NAME="install-arm-toolchain.sh"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/tools/lib/common.sh"

toolchain_version="15.2.rel1"
host_arch="x86_64"
target_triple="arm-none-eabi"
install_dir="${RUNNER_TEMP:-$ROOT/.work}/arm-gnu-toolchain-15.2.rel1"
expected_version_fragment="Arm GNU Toolchain 15.2.Rel1"

usage() {
  cat <<'EOF'
Usage: v2_qmk_fw/tools/install-arm-toolchain.sh [options]

Options:
  --install-dir <path>
  --version <value>
EOF
}

resolve_path() {
  case "$1" in
    /*) printf '%s\n' "$1" ;;
    *) printf '%s\n' "$PWD/$1" ;;
  esac
}

verify_installed_toolchain() {
  local gcc_bin="$1"
  local version_line=""

  if [ "$(uname -s)" != "Linux" ]; then
    log "skipping compiler version check on non-Linux host"
    return 0
  fi

  if [ "$(uname -m)" != "$host_arch" ]; then
    log "skipping compiler version check on unsupported host architecture: $(uname -m)"
    return 0
  fi

  version_line="$("$gcc_bin" --version | head -n 1)"
  printf '%s\n' "$version_line" | grep -Fq "$expected_version_fragment" \
    || die "installed compiler did not match expected toolchain '$expected_version_fragment': $version_line"

  log "verified compiler version: $version_line"
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --install-dir)
      install_dir="$2"
      shift 2
      ;;
    --version)
      toolchain_version="$2"
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

ensure_tool curl
ensure_tool tar

archive_name="arm-gnu-toolchain-${toolchain_version}-${host_arch}-${target_triple}.tar.xz"
archive_path="$(dirname "$install_dir")/${archive_name}"
extracted_dir_name="${archive_name%.tar.xz}"
archive_urls=(
  "https://developer.arm.com/-/media/Files/downloads/gnu/${toolchain_version}/binrel/${archive_name}"
  "https://armkeil.blob.core.windows.net/developer/files/downloads/gnu/${toolchain_version}/binrel/${archive_name}"
)
archive_url=""

install_dir="$(resolve_path "$install_dir")"
ensure_dir "$(dirname "$install_dir")"
extracted_dir_path="$(dirname "$install_dir")/${extracted_dir_name}"

if [ ! -x "$install_dir/bin/arm-none-eabi-gcc" ]; then
  rm -f "$archive_path"

  for candidate_url in "${archive_urls[@]}"; do
    log "attempting download from $candidate_url"
    if curl --fail --location --retry 3 --retry-delay 2 --output "$archive_path" "$candidate_url"; then
      archive_url="$candidate_url"
      break
    fi
  done

  [ -n "$archive_url" ] || die "failed to download $archive_name from all configured URLs"

  log "extracting $archive_path"
  rm -rf "$install_dir"
  if [ "$extracted_dir_path" != "$install_dir" ]; then
    rm -rf "$extracted_dir_path"
  fi
  tar -xJf "$archive_path" -C "$(dirname "$install_dir")"

  if [ "$extracted_dir_path" != "$install_dir" ]; then
    log "relocating extracted toolchain to $install_dir"
    mv "$extracted_dir_path" "$install_dir"
  fi
else
  archive_url="${archive_urls[0]}"
fi

[ -x "$install_dir/bin/arm-none-eabi-gcc" ] || die "arm-none-eabi-gcc not found after install: $install_dir/bin/arm-none-eabi-gcc"
# CI pins the Arm toolchain explicitly because ubuntu-latest package drift once
# produced smaller release firmware that built but did not boot on hardware.
verify_installed_toolchain "$install_dir/bin/arm-none-eabi-gcc"

if [ -n "${GITHUB_PATH:-}" ]; then
  printf '%s\n' "$install_dir/bin" >>"$GITHUB_PATH"
fi

if [ -n "${GITHUB_ENV:-}" ]; then
  {
    printf 'ARM_GNU_TOOLCHAIN_DIR=%s\n' "$install_dir"
    printf 'ARM_GNU_TOOLCHAIN_BIN=%s\n' "$install_dir/bin"
    printf 'ARM_GNU_TOOLCHAIN_URL=%s\n' "$archive_url"
  } >>"$GITHUB_ENV"
fi

log "installed Arm GNU Toolchain in $install_dir"
log "binary path: $install_dir/bin/arm-none-eabi-gcc"
log "download URL: $archive_url"
