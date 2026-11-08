#pragma once
// Scripted input for openartemis_drive (see drive.cpp). Not in the player build.
#include <SDL3/SDL_video.h>

#include "core/runtime/runtime.h"

namespace oa::drive {

/// Run the script for one frame (before the engine tick). False = quit.
bool step(oa::runtime::App& app, SDL_Window* window);

} // namespace oa::drive
