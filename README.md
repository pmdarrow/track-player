# Track Player

An Audio Unit instrument plugin for macOS that plays a playlist of supported
audio files, with a standalone app target for development. Built with
[JUCE](https://juce.com/) 8 and CMake.

Features:

- Add and remove supported audio files from anywhere on disk, including WAV,
  AIFF, FLAC, OGG, MP3, M4A, AAC, and other CoreAudio-backed formats on macOS.
- Play / pause and seek via a draggable progress slider.
- Single click to select a track, double click (or the play button) to play it.
- Auto-advances to the next track at end-of-file.
- Playlist and current track persist in the plugin's host state.

## Requirements

- macOS 10.13 or later (Intel or Apple Silicon)
- [CMake](https://cmake.org/) 3.22 or later
- [Ninja](https://ninja-build.org/) (`brew install ninja`)
- Xcode command-line tools (`xcode-select --install`) — a full Xcode install is
  recommended so the AU can be validated with `auval`
- Git (used by CMake's `FetchContent` to pull JUCE)

Optional, for dev tooling:

- `brew install llvm` — provides `clang-format` and `clang-tidy` used by the
  `format` / `format-check` / `tidy` CMake targets and the git pre-commit hook.

## Building

From the project root:

```bash
cmake -B build -G Ninja
cmake --build build
```

The first configure step clones JUCE 8.0.12 into `build/_deps/` via
`FetchContent`; this can take a few minutes. Subsequent builds are incremental.

On success two artefacts are produced:

```
build/TrackPlayer_artefacts/AU/Track Player.component   # the AU plugin
build/TrackPlayer_artefacts/Standalone/Track Player.app # dev-only standalone app
```

The standalone app is handy for iterating on the UI without having to open a
DAW — it wraps the same editor in a window and routes audio to the default
output device.

For a universal (Intel + Apple Silicon) distribution build, override the
architecture at configure time:

```bash
cmake -B build-universal -G Ninja -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"
cmake --build build-universal
```

Because `COPY_PLUGIN_AFTER_BUILD` is enabled in [CMakeLists.txt](CMakeLists.txt),
the bundle is also copied into the user plugin folder:

```
~/Library/Audio/Plug-Ins/Components/Track Player.component
```

### Validating the plugin (optional)

macOS caches AU metadata aggressively. After the first install (or any time you
re-build) you may want to:

```bash
killall -9 AudioComponentRegistrar || true
auval -v aumu Trkp Pdar
```

`aumu` is the AU type for music devices / instruments. `Trkp` is this plugin's
four-character code and `Pdar` is the manufacturer code — both defined in
[CMakeLists.txt](CMakeLists.txt).

## Using the plugin

1. Launch an AU-compatible host (Logic Pro, GarageBand, Ableton Live 11+,
   Reaper, AUM, …).
2. Create a new instrument track and choose **Peter Darrow → Track Player**.
3. Click **Add** to pick one or more supported audio files (from anywhere on
   disk) and drop them into the playlist. **Remove** deletes the selected track.
4. Single-click a row to select it. Double-click a row (or use the round play
   button in the transport row) to start playback. The progress slider scrubs
   the current track; seeks commit on release. Playback auto-advances to the
   next track at end-of-file.

The playlist and the currently-loaded track are persisted in the plugin's host
state, so reopening a session restores everything (files whose paths no longer
resolve are silently dropped).

## Development setup

All dev tooling is driven by CMake targets and a checked-in git hook — no
Python/Node frameworks required.

The plugin is built with Apple Clang (standard for the macOS AU flow);
`clang-format` and `clang-tidy` come from Homebrew's keg-only `llvm`, which
the CMake targets and git hook locate automatically.

### One-time setup after cloning

```bash
brew install llvm                           # clang-format + clang-tidy
git config core.hooksPath .githooks         # activate the checked-in hook
```

The pre-commit hook ([.githooks/pre-commit](.githooks/pre-commit)) runs
`clang-format --dry-run` against staged C/C++ files and blocks the commit if
anything would be reformatted. Bypass with `git commit --no-verify`.

### Available CMake targets

| Target          | What it does                                              |
| --------------- | --------------------------------------------------------- |
| `format`        | Rewrite sources in place via clang-format.                |
| `format-check`  | Fail if any source needs reformatting. Useful in CI.      |
| `tidy`          | Run clang-tidy across the sources.                        |

Run any of them with `cmake --build build --target <name>`.
