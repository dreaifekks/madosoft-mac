#pragma once
// macOS app bundle launch: a per-game .app carries Contents/Resources/game.conf
// (written by macos/package_macos.py) instead of command-line arguments.
#include <string>

#include "core/runtime/runtime.h"

namespace oa::macos {

/// Fill `cfg` (project, overlays, save root) and `title` from the bundle's
/// game.conf. Returns false when there is no bundle config or the user chose
/// to quit while the game's disk was not connected.
bool load_bundle(oa::runtime::AppConfig& cfg, std::string& title);

} // namespace oa::macos
