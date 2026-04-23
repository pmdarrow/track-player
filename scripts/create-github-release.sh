#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: bash scripts/create-github-release.sh [--draft] [--prerelease] [--dry-run]

Builds the AU, packages Simple Audio Player.component with install.sh, then creates a
GitHub release and uploads the zip with gh. The release tag is derived from
project(... VERSION ...) in CMakeLists.txt.

Examples:
  bash scripts/create-github-release.sh
  bash scripts/create-github-release.sh --draft
  bash scripts/create-github-release.sh --dry-run

Environment overrides:
  SIMPLE_AUDIO_PLAYER_RELEASE_BUILD_DIR   Build dir. Default: build-release
  SIMPLE_AUDIO_PLAYER_RELEASE_ARCHS       CMAKE_OSX_ARCHITECTURES. Default: x86_64;arm64
  CMAKE_GENERATOR                  CMake generator. Default: Ninja
USAGE
}

die() {
  echo "error: $*" >&2
  exit 1
}

dry_run=0
gh_flags=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --draft)
      gh_flags+=(--draft)
      ;;
    --prerelease)
      gh_flags+=(--prerelease)
      ;;
    --dry-run)
      dry_run=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
  shift
done

repo_root="$(git rev-parse --show-toplevel)"
cd "${repo_root}"

[[ "$(uname -s)" == "Darwin" ]] || die "AU releases must be built on macOS."
command -v cmake >/dev/null 2>&1 || die "cmake is required."
command -v ditto >/dev/null 2>&1 || die "ditto is required."
command -v codesign >/dev/null 2>&1 || die "codesign is required."

branch="$(git branch --show-current)"
[[ -n "${branch}" ]] || die "cannot create a release from a detached HEAD."

if [[ "${dry_run}" -eq 0 ]]; then
  command -v gh >/dev/null 2>&1 || die "gh is required. Install it with: brew install gh"
  [[ -z "$(git status --porcelain)" ]] || die "working tree is not clean; commit or stash changes before releasing."

  upstream="$(git rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null)" ||
    die "current branch has no upstream; push it before releasing."
  git fetch --quiet --tags
  local_head="$(git rev-parse HEAD)"
  remote_head="$(git rev-parse "${upstream}")"
  [[ "${local_head}" == "${remote_head}" ]] ||
    die "local ${branch} does not match ${upstream}; push or pull before releasing."
fi

build_dir="${SIMPLE_AUDIO_PLAYER_RELEASE_BUILD_DIR:-build-release}"
archs="${SIMPLE_AUDIO_PLAYER_RELEASE_ARCHS:-x86_64;arm64}"
generator="${CMAKE_GENERATOR:-Ninja}"

if [[ "${build_dir}" = /* ]]; then
  build_dir_abs="${build_dir}"
else
  build_dir_abs="${repo_root}/${build_dir}"
fi

cmake -S "${repo_root}" -B "${build_dir}" -G "${generator}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSIMPLE_AUDIO_PLAYER_COPY_PLUGIN_AFTER_BUILD=FALSE \
  "-DCMAKE_OSX_ARCHITECTURES=${archs}"

version="$(awk -F= '$1 == "CMAKE_PROJECT_VERSION:STATIC" { print $2; exit }' "${build_dir_abs}/CMakeCache.txt")"
[[ -n "${version}" ]] || die "could not read CMAKE_PROJECT_VERSION from ${build_dir_abs}/CMakeCache.txt"

tag="v${version}"
package_name="simple-audio-player-${version}-macos"
dist_dir="${repo_root}/dist"
stage_dir="${dist_dir}/${package_name}"
zip_path="${dist_dir}/${package_name}.zip"
component_stage="${stage_dir}/Simple Audio Player.component"

if [[ "${dry_run}" -eq 0 ]]; then
  existing_tag_commit="$(git rev-parse -q --verify "refs/tags/${tag}^{commit}" 2>/dev/null || true)"

  if [[ -n "${existing_tag_commit}" && "${existing_tag_commit}" != "${local_head}" ]]; then
    die "tag ${tag} already exists at ${existing_tag_commit}, but HEAD is ${local_head}"
  fi
fi

echo "Using Simple Audio Player ${version} (${tag})"

cmake --build "${build_dir}" --target SimpleAudioPlayer_AU --config Release

component_candidates=(
  "${build_dir_abs}/SimpleAudioPlayer_artefacts/Release/AU/Simple Audio Player.component"
  "${build_dir_abs}/SimpleAudioPlayer_artefacts/AU/Simple Audio Player.component"
)

component_src=""
for candidate in "${component_candidates[@]}"; do
  if [[ -d "${candidate}" ]]; then
    component_src="${candidate}"
    break
  fi
done

if [[ -z "${component_src}" ]]; then
  echo "error: built component was not found. Checked:" >&2
  printf '  %s\n' "${component_candidates[@]}" >&2
  exit 1
fi

rm -rf "${stage_dir}" "${zip_path}"
mkdir -p "${stage_dir}"

ditto --noqtn "${component_src}" "${component_stage}"
cp "${repo_root}/scripts/install.sh" "${stage_dir}/install.sh"
chmod 755 "${stage_dir}/install.sh"
xattr -dr com.apple.quarantine "${stage_dir}" 2>/dev/null || true

codesign --force --deep --timestamp=none --sign - "${component_stage}"
codesign --verify --deep --strict "${component_stage}"

(
  cd "${stage_dir}"
  ditto -c -k --sequesterRsrc --zlibCompressionLevel 9 . "${zip_path}"
)

echo "Created ${zip_path}"

if [[ "${dry_run}" -eq 1 ]]; then
  echo "Dry run: skipped GitHub release creation."
  exit 0
fi

release_notes="macOS release.

Install:
curl -fsSL https://raw.githubusercontent.com/pmdarrow/simple-audio-player/main/scripts/install.sh | bash

Restart your DAW or rescan Audio Units after installing.

This build is intended for trusted manual installation and is not notarized."

gh release create "${tag}" "${zip_path}#Simple Audio Player macOS installer" \
  --target "${branch}" \
  --title "Simple Audio Player ${tag}" \
  --notes "${release_notes}" \
  "${gh_flags[@]}"
