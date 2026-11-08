// oa::plat desktop implementation — shared by linux and windows.
// Compiled when the target is neither Android nor wasm.
//
// Desktop answers:
//   - default_save_root(): the game data directory itself (parent of a .pfs
//     archive, or the extracted project tree when started as a directory).
//     This is the pre-platform-layer rule (host_save_root) moved verbatim:
//     env override and the test-build autodrive temp fallback
//     stay in main.cpp as host policy; only the platform default lives here.
//     Windows is byte-identical to linux: the engine path contract ('/' +
//     ASCII, case-insensitive, traversal-guarded) and the
//     std::filesystem host ops make the save-root rule platform-independent.
//     TODO: split platform_windows.cpp when a real divergence appears.
//     macOS differs: saves go under ~/Library/Application Support, never
//     into the game folder (see default_save_root).
//   - lifecycle: nothing. The desktop loop is never blocked by the OS (an
//     unfocused/occluded window keeps iterating), so no paused gap exists
//     and SDL never delivers the iOS/Android-only background/foreground
//     events (SDL_events.h) — take_lifecycle_resume() is always false.
//   - init()/shutdown(): no platform services to boot/tear down beyond SDL3
//     itself (main.cpp owns the SDL_Init/SDL_Quit pairing on the windowed
//     path; headless never touches SDL).
#include "platform/Platform.h"

#include <cstdlib>
#include <filesystem>

namespace oa::plat {

std::string default_save_root(const std::string& data_path, bool data_is_dir) {
    // Game dir = the folder tree itself / the parent of a .pfs archive.
    std::string save_root = (data_is_dir ? std::filesystem::path(data_path)
                                         : std::filesystem::path(data_path).parent_path())
                                .string();
    if (save_root.empty()) save_root = ".";
#if defined(__APPLE__)
    // ~/Library/Application Support/Madosoft Artemis/<game folder>/save: the
    // game folder is often a read-only or external disk.
    std::error_code ec;
    std::filesystem::path game = std::filesystem::absolute(save_root, ec).lexically_normal();
    if (!game.has_filename()) game = game.parent_path(); // "dir/" names dir
    const char* home = std::getenv("HOME");
    save_root = (std::filesystem::path(home ? home : "") /
                 "Library/Application Support/Madosoft Artemis" / game.filename() / "save")
                    .string();
#endif
    return save_root;
}

void init() {
    // Nothing to do on desktop: see the TU header comment.
}

void shutdown() {
    // Nothing to do on desktop: see the TU header comment.
}

bool take_lifecycle_resume() {
    // Desktop has no OS background/foreground round trips (see the TU
    // header comment); always false keeps the host's per-frame poll free.
    return false;
}

} // namespace oa::plat
