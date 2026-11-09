# Sources and attribution

This repository is a fork of openartemis. The `macos-madosoft` branch adds a
macOS (Apple Silicon) host and one-command packaging of one `.app` per game for
two まどそふと titles, ハミダシクリエイティブ and ハミダシクリエイティブ凸.
openartemis is offered under MIT OR GPL-3.0-or-later; this repository takes the
MIT option, and the whole repository, including the code added on this branch,
is released under the MIT License in `LICENSE`. Game data, files extracted from
game launchers or patches, and game icons are not part of the repository.

## openartemis

- Project: https://github.com/luxiaoling-mc/openartemis
- Copyright (c) 2026 deepseek. License: MIT OR GPL-3.0-or-later; used here
  under MIT.
- Base: upstream commit `65725c3` (2026-11-05).
- The engine is upstream's: PFS reading, the IET/AST script interpreter and its
  Lua 5.1 bridge, layers, text, audio, video and saves. This branch changes or
  adds only these files; `git log 65725c3..` has every change:
  - build: `CMakePresets.json` (preset `macos-arm64`),
    `cmake/triplets/arm64-osx-14.cmake`, `src/app/CMakeLists.txt`;
  - overlays mounted above a project: `src/core/fs/physfs_fs.*`;
  - input, close request, `[dialog]` and fullscreen handling:
    `src/core/runtime/runtime*.{cpp,h}`, `src/core/media/audio.cpp`
    (a diagnostic log);
  - macOS host: `src/app/main.cpp`, `src/app/macos_bundle.*`,
    `src/app/platform/*` (save root under Application Support);
  - scripted test host: `src/app/drive.*`;
  - packaging and docs: `macos/package_macos.py`, `macos/README.md`,
    `docs/TESTING.md`, `README.md`, this file;
  - license: `LICENSE` keeps only the MIT text, with upstream's copyright line.
- Upstream vendors Lua 5.1.5 in `third_party/lua-5.1.5` (MIT, Lua.org,
  PUC-Rio; notice in `third_party/lua-5.1.5/COPYRIGHT`). Upstream's Android
  launcher and web shell are adapted from krkrsdl3, as `docs/PLATFORMS.md`
  describes; this branch does not change them.

## evbunpack

- Project: https://github.com/mos9527/evbunpack, by mos9527.
- License: Apache-2.0.
- `macos/package_macos.py` runs it as an external command, at packaging time
  only, to extract the files a launcher or patch packed with Enigma Virtual Box
  maps into the game. It is not vendored, linked or copied into the app;
  install it separately, as `macos/README.md` shows.

## Libraries linked into the macOS player

The `macos-arm64` preset builds these with vcpkg (baseline in `vcpkg.json`) and
links them statically into `openartemis`. Versions are the ones the preset
resolved on 2026-11-09; licenses are as the vcpkg ports record them.

| Library | Version | License |
|---|---|---|
| SDL3 | 3.4.12 | Zlib, with Apache-2.0 and MIT parts |
| FreeType | 2.14.3 | FTL or GPL-2.0-or-later |
| libpng | 1.6.58 | libpng-2.0 |
| libjpeg-turbo | 3.2.0 | IJG, BSD-3-Clause, Zlib |
| libogg | 1.3.6 | BSD-3-Clause |
| libvorbis | 1.3.7 | BSD-3-Clause |
| libtheora | 1.2.0 | BSD-3-Clause |
| FFmpeg: libavcodec, libavformat, libavutil | 6.1.1 | LGPL-2.1-or-later; vcpkg features `avcodec` and `avformat` only, no `gpl`, `version3` or `nonfree` |
| PhysicsFS | 3.2.0 | Zlib |
| zlib | 1.3.2 | Zlib |
| bzip2 | 1.0.8 | bzip2-1.0.6 |
| Brotli | 1.2.0 | MIT |

The repository ships no binaries. Whoever distributes a built app has to meet
these licenses, in particular the LGPL terms for FFmpeg linked statically.

## Games

The packaging and the tested scope in `macos/README.md` concern the user's own
copies of ハミダシクリエイティブ and ハミダシクリエイティブ凸 (まどそふと). The apps
read those files in place and never write to the game folders. Apps built with
a game's icon are for local use and should not be distributed.
