// openartemis_drive: the normal host plus a scripted input source, for
// unattended play-through checks (frames are dumped to PPM). Script = one
// command per line, run in order, from the file named by OA_DRIVE_SCRIPT:
//
//   wait N            let N frames pass
//   waitms N          let N milliseconds of wall time pass
//   move X Y          pointer to window point
//   click X Y         left press (this frame) + release (next frame)
//   rclick X Y        same with the right button
//   key VK            Windows virtual-key press + release (13 Enter, 112 F1 ...)
//   hold VK / release VK
//   wheel D           one wheel notch (D > 0 up)
//   clicks N EVERY X Y  N left clicks at X,Y, one every EVERY frames
//   dump PATH         write the current frame as PPM
//   state             print the OA_SCENEDUMP / OA_TXTDUMP / OA_VARDUMP reports
//   resize W H        set the window size
//   fullscreen 0|1
//   quit
#include "drive.h"

#include <SDL3/SDL_timer.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace oa::drive {

namespace {

struct State {
    std::vector<std::string> lines;
    size_t next = 0;
    uint64_t wait_frames = 0;
    uint64_t wait_until_ms = 0;
    int release_vk = 0;     // key to release next frame
    int release_button = 0; // mouse button to release next frame
    // clicks N EVERY X Y
    int clicks_left = 0;
    int clicks_every = 0;
    float clicks_x = 0, clicks_y = 0;
    bool loaded = false;
};

State& state()
{
    static State s;
    if (!s.loaded) {
        s.loaded = true;
        const char* path = std::getenv("OA_DRIVE_SCRIPT");
        std::ifstream in(path ? path : "");
        for (std::string line; std::getline(in, line);) {
            const auto hash = line.find('#');
            if (hash != std::string::npos) line.resize(hash);
            if (line.find_first_not_of(" \t\r") != std::string::npos) s.lines.push_back(line);
        }
        std::printf("[drive] %zu commands from %s\n", s.lines.size(), path ? path : "(none)");
    }
    return s;
}

} // namespace

bool step(oa::runtime::App& app, SDL_Window* window)
{
    State& s = state();
    if (s.release_vk) {
        app.key_event(s.release_vk, false);
        s.release_vk = 0;
    }
    if (s.release_button) {
        app.mouse_button(s.release_button, false);
        s.release_button = 0;
    }
    if (s.wait_frames > 0) {
        --s.wait_frames;
        return true;
    }
    if (s.wait_until_ms > SDL_GetTicks()) return true;
    if (s.clicks_left > 0) {
        app.mouse_move(s.clicks_x, s.clicks_y);
        app.mouse_button(1, true);
        s.release_button = 1;
        --s.clicks_left;
        s.wait_frames = uint64_t(s.clicks_every);
        return true;
    }
    while (s.next < s.lines.size()) {
        const std::string line = s.lines[s.next++];
        std::istringstream in(line);
        std::string cmd;
        in >> cmd;
        std::printf("[drive] t=%llums %s\n", (unsigned long long)SDL_GetTicks(), line.c_str());
        std::fflush(stdout);
        if (cmd == "wait") {
            in >> s.wait_frames;
            return true;
        } else if (cmd == "waitms") {
            uint64_t ms = 0;
            in >> ms;
            s.wait_until_ms = SDL_GetTicks() + ms;
            return true;
        } else if (cmd == "move" || cmd == "click" || cmd == "rclick") {
            float x = 0, y = 0;
            in >> x >> y;
            app.mouse_move(x, y);
            if (cmd == "move") continue;
            const int button = cmd == "click" ? 1 : 2;
            app.mouse_button(button, true);
            s.release_button = button;
            return true;
        } else if (cmd == "key") {
            in >> s.release_vk;
            app.key_event(s.release_vk, true);
            return true;
        } else if (cmd == "hold" || cmd == "release") {
            int vk = 0;
            in >> vk;
            app.key_event(vk, cmd == "hold");
        } else if (cmd == "wheel") {
            int d = 0;
            in >> d;
            app.mouse_wheel(d);
            return true;
        } else if (cmd == "clicks") {
            in >> s.clicks_left >> s.clicks_every >> s.clicks_x >> s.clicks_y;
            return true;
        } else if (cmd == "dump") {
            std::string path;
            std::getline(in >> std::ws, path);
            app.dump(path);
        } else if (cmd == "state") {
            app.dump_state();
        } else if (cmd == "resize") {
            int w = 0, h = 0;
            in >> w >> h;
            SDL_SetWindowSize(window, w, h);
        } else if (cmd == "fullscreen") {
            int on = 0;
            in >> on;
            SDL_SetWindowFullscreen(window, on != 0);
        } else if (cmd == "quit") {
            return false;
        } else {
            std::fprintf(stderr, "[drive] unknown command: %s\n", line.c_str());
        }
    }
    return true;
}

} // namespace oa::drive
