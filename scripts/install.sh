#!/usr/bin/env bash
set -euo pipefail

PLUGIN_NAME="Simple Audio Player.component"
INSTALL_DIR="/Library/Audio/Plug-Ins/Components"
REPO_SLUG="pmdarrow/simple-audio-player"
ASSET_NAME_PATTERN="simple-audio-player-[0-9A-Za-z._-]+-macos\\.zip"
GITHUB_API_URL="https://api.github.com/repos/${REPO_SLUG}/releases/latest"

target_component="${INSTALL_DIR}/${PLUGIN_NAME}"
tmp_dir=""

die() {
  echo "error: $*" >&2
  exit 1
}

cleanup() {
  if [[ -n "${tmp_dir}" ]]; then
    rm -rf "${tmp_dir}"
  fi
}
trap cleanup EXIT

if [[ "$(uname -s)" != "Darwin" ]]; then
  die "Audio Unit plugins can only be installed on macOS."
fi

find_local_component() {
  if [[ -n "${BASH_SOURCE[0]:-}" && -f "${BASH_SOURCE[0]}" ]]; then
    local script_dir
    script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

    if [[ -d "${script_dir}/${PLUGIN_NAME}" ]]; then
      printf '%s\n' "${script_dir}/${PLUGIN_NAME}"
    fi
  fi
}

download_latest_component() {
  command -v curl >/dev/null 2>&1 || die "curl is required to download the release."
  command -v ditto >/dev/null 2>&1 || die "ditto is required to extract the release zip."

  tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/simple-audio-player-install.XXXXXX")"
  local zip_path="${tmp_dir}/simple-audio-player-macos.zip"
  local extract_dir="${tmp_dir}/release"

  echo "Finding the latest Simple Audio Player release..." >&2

  local asset_urls
  asset_urls="$(curl -fsSL "${GITHUB_API_URL}" | grep -Eo "https://[^\"]+/${ASSET_NAME_PATTERN}" || true)"

  local asset_url
  asset_url="$(printf '%s\n' "${asset_urls}" | head -n 1)"

  if [[ -z "${asset_url}" ]]; then
    die "could not find a macOS release zip on the latest GitHub release."
  fi

  echo "Downloading ${asset_url}..." >&2
  curl -fL "${asset_url}" -o "${zip_path}"

  mkdir -p "${extract_dir}"
  ditto -x -k "${zip_path}" "${extract_dir}"

  local component
  component="$(find "${extract_dir}" -maxdepth 3 -type d -name "${PLUGIN_NAME}" -print -quit)"

  if [[ -z "${component}" ]]; then
    die "${PLUGIN_NAME} was not found inside the release zip."
  fi

  source_component="${component}"
}

source_component="$(find_local_component)"
if [[ -z "${source_component}" ]]; then
  download_latest_component
fi

echo "Installing ${PLUGIN_NAME} system-wide..."
echo "Destination: ${target_component}"
echo "You may be prompted for your macOS password."

sudo mkdir -p "${INSTALL_DIR}"
sudo rm -rf "${target_component}"
sudo ditto --noqtn "${source_component}" "${target_component}"

# Downloads and unzipped archives can carry Gatekeeper quarantine metadata.
sudo xattr -dr com.apple.quarantine "${target_component}" 2>/dev/null || true

if command -v codesign >/dev/null 2>&1; then
  sudo codesign --force --deep --timestamp=none --sign - "${target_component}"
  codesign --verify --deep --strict "${target_component}"
else
  echo "warning: codesign was not found; installed without ad-hoc signing." >&2
fi

killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true

echo "Installed ${PLUGIN_NAME}."
echo "Restart your DAW or rescan Audio Units if it was already open."
