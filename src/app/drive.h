#pragma once
// Scripted input for openartemis_drive (see drive.cpp). Not in the player build.
#include <SDL3/SDL_video.h>

#include <functional>

#include "core/runtime/runtime.h"

namespace oa::drive {

/// Run the script for one frame (before the engine tick). False = quit.
/// `set_fullscreen` is the host's own fullscreen switch.
bool step(oa::runtime::App& app, SDL_Window* window,
          const std::function<void(bool)>& set_fullscreen);

/// The host's [dialog] box: logs it and returns the script's `dialog N`.
int dialog(const std::string& title, const std::string& message, bool yes_no);

} // namespace oa::drive
