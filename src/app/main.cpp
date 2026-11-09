// openartemis — SDL layer. All SDL types/events stay here; the engine host
// only receives translated input and frame ticks.
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <utility>

#include "core/runtime/runtime.h"
#include "platform/Platform.h"
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>

#include "macos_bundle.h"
#endif
#if defined(OA_DRIVE)
#include "drive.h"
#endif

namespace {

enum class TouchPhase { Idle, SingleFinger, MultiFinger };

struct TouchFinger {
    float x = 0.0f;
    float y = 0.0f;
    float start_x = 0.0f;
    float start_y = 0.0f;
    bool moved = false;
};

struct HostState {
    oa::runtime::App* app = nullptr;
    SDL_Window* window = nullptr;
    bool sdl_inited = false;
    bool plat_inited = false;

    TouchPhase touch_phase = TouchPhase::Idle;
    std::map<SDL_FingerID, TouchFinger> fingers;
    Uint8 pending_button = 0;
    float pending_x = 0.0f;
    float pending_y = 0.0f;
    std::map<SDL_Keycode, int> command_keys; // held ⌘ key -> the F key sent for it
    bool fullscreen = false; // last state asked for or reported by the window
    // Inside App::tick. A native [dialog] runs its modal loop there, and a
    // resize or zoom of the window under it makes SDL's live-resize iterate or
    // the zoom timer fire in that loop. The engine waits for the box instead,
    // as the Windows engine stands still under its message box.
    bool ticking = false;
#if defined(__APPLE__)
    CFRunLoopTimerRef animation_timer = nullptr;
    oa::runtime::AppResult animation_result = oa::runtime::AppResult::Continue;
#endif
};

// Fullscreen switches are asynchronous on macOS: record the requested state at
// once (the enter/leave events confirm it) so the game's config page, which
// reads it back every frame, neither flips its button back nor toggles twice.
void set_fullscreen(HostState* host, bool on)
{
    if (!SDL_SetWindowFullscreen(host->window, on)) return;
    host->fullscreen = on;
    host->app->set_fullscreen(on);
}

#if defined(__APPLE__)
// AppKit animates a window zoom (the zoom button, SDL_MaximizeWindow) in a
// private run loop mode and holds the main loop until it ends, about 0.4 s.
// The mixer runs on frame ticks and keeps ~0.16 s queued, so the music dropped
// out. Frames go on from a timer in that mode, as SDL keeps them going through
// a live resize.
void animation_tick(CFRunLoopTimerRef, void* info)
{
    auto* host = static_cast<HostState*>(info);
    if (host->ticking || host->animation_result != oa::runtime::AppResult::Continue) return;
    host->ticking = true;
    host->animation_result = host->app->tick(false);
    host->ticking = false;
}

void tick_through_window_animations(HostState* host)
{
    CFRunLoopTimerContext context = {0, host, nullptr, nullptr, nullptr};
    host->animation_timer = CFRunLoopTimerCreate(nullptr, CFAbsoluteTimeGetCurrent(), 1.0 / 60.0,
                                                 0, 0, animation_tick, &context);
    CFRunLoopAddTimer(CFRunLoopGetMain(), host->animation_timer,
                      CFSTR("_NSMoveTimerRunLoopMode"));
}
#endif

#if defined(__APPLE__) && !defined(OA_DRIVE)
// [dialog]: the native engine's OS message box (the game's own wording, e.g.
// the close button's "quit the game?"), modal over the game window.
int show_dialog(SDL_Window* window, const std::string& title, const std::string& message,
                bool yes_no)
{
    const SDL_MessageBoxButtonData yes_no_buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "是"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "否"},
    };
    const SDL_MessageBoxButtonData ok_button[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT | SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 1,
         "确定"},
    };
    const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, window, title.c_str(),
                                    message.c_str(), yes_no ? 2 : 1,
                                    yes_no ? yes_no_buttons : ok_button, nullptr};
    int choice = 0;
    SDL_ShowMessageBox(&box, &choice);
    return choice;
}
#endif

void print_usage(const char* argv0)
{
    std::printf(
        "usage: %s [options] [project.pfs]\n"
        "\n"
        "options:\n"
        "  --frames N           run N frames then exit\n"
        "  --dump PATH          write the final frame as PPM\n"
        "  --headless           no window/renderer, fixed 16ms ticks\n"
        "  --fixed-delta-ms N   deterministic tick step (windowed too)\n"
        "  --fps N              target frame rate (default: real-time pacing)\n"
        "  --platform NAME      project platform (default: build OS)\n"
        "  --renderer NAME      'sdl' (default) or 'gles'\n"
        "  --mute               mute audio\n"
        "  --overlay PATH       mount a .pfs or directory above the project\n"
        "                       (repeatable, the last one wins)\n"
        "  -h, --help           show this help\n"
        "\n"
        "env:\n"
        "  OA_PFS=path          default project\n"
        "  OA_SAVE_ROOT=path    override the writable save root\n"
        "  OA_RENDERER=sdl|gles default renderer\n",
        argv0 ? argv0 : "openartemis");
}

// 0 = ok, 1 = help printed, 2 = usage error printed.
int parse_args(int argc, char** argv, oa::runtime::AppConfig& cfg)
{
#if defined(__ANDROID__)
    cfg.platform = "android";
#elif defined(__EMSCRIPTEN__)
    cfg.platform = "wasm";
#else
    cfg.platform = "windows";
#endif
    if (const char* p = std::getenv("OA_PFS"); p && *p) cfg.project = p;
    if (const char* r = std::getenv("OA_RENDERER"); r && *r) cfg.renderer = r;

    auto usage_error = [&](const std::string& msg) {
        std::fprintf(stderr, "openartemis: %s\n", msg.c_str());
        print_usage(argv[0]);
        return 2;
    };

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            return 1;
        }

        const auto eq = a.find('=');
        const std::string name = eq == std::string::npos ? a : a.substr(0, eq);
        auto value_of = [&](std::string& out) -> bool {
            if (eq != std::string::npos) {
                out = a.substr(eq + 1);
                return true;
            }
            if (i + 1 < argc) {
                out = argv[++i];
                return true;
            }
            return false;
        };

        if (name == "--mute") {
            cfg.mute = true;
        } else if (name == "--headless") {
            cfg.headless = true;
        } else if (name == "--frames") {
            std::string v;
            if (!value_of(v)) return usage_error("--frames needs a positive frame count");
            char* end = nullptr;
            errno = 0;
            const unsigned long long n = std::strtoull(v.c_str(), &end, 10);
            if (errno || !end || *end || n == 0)
                return usage_error("--frames: expected a positive integer, got '" + v + "'");
            cfg.frames_target = n;
        } else if (name == "--fixed-delta-ms") {
            std::string v;
            if (!value_of(v)) return usage_error("--fixed-delta-ms needs a positive value");
            char* end = nullptr;
            errno = 0;
            const long n = std::strtol(v.c_str(), &end, 10);
            if (errno || !end || *end || n <= 0)
                return usage_error("--fixed-delta-ms: expected a positive integer, got '" + v + "'");
            cfg.fixed_delta_ms = static_cast<int>(n);
        } else if (name == "--dump") {
            std::string v;
            if (!value_of(v) || v.empty()) return usage_error("--dump needs a file path");
            cfg.dump = v;
        } else if (name == "--fps") {
            std::string v;
            if (!value_of(v)) return usage_error("--fps needs a positive frame rate");
            char* end = nullptr;
            errno = 0;
            const long n = std::strtol(v.c_str(), &end, 10);
            if (errno || !end || *end || n <= 0)
                return usage_error("--fps: expected a positive integer, got '" + v + "'");
            cfg.fps = static_cast<int>(n);
        } else if (name == "--platform") {
            std::string v;
            if (!value_of(v) || v.empty()) return usage_error("--platform needs a name");
            cfg.platform = v;
        } else if (name == "--overlay") {
            std::string v;
            if (!value_of(v) || v.empty()) return usage_error("--overlay needs a path");
            cfg.overlays.push_back(v);
        } else if (name == "--renderer") {
            std::string v;
            if (!value_of(v) || v.empty()) return usage_error("--renderer needs a name");
            cfg.renderer = v;
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            return usage_error("unknown option '" + a + "'");
        } else {
            cfg.project = a;
        }
    }

    if (cfg.renderer != "sdl" && cfg.renderer != "gles")
        return usage_error("--renderer: expected 'sdl' or 'gles', got '" +
                           cfg.renderer + "'");
    return 0;
}

int sdl_key_to_vk(SDL_Keycode key)
{
    if (key == SDLK_ESCAPE) return 0;
    SDL_Keymod mods = 0;
    const SDL_Scancode sc = SDL_GetScancodeFromKey(key, &mods);
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z)
        return 'A' + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_0 && sc <= SDL_SCANCODE_9)
        return '0' + (sc - SDL_SCANCODE_0);
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12)
        return 112 + (sc - SDL_SCANCODE_F1);
    switch (key) {
        case SDLK_RETURN: return 13;
        case SDLK_SPACE: return 32;
        case SDLK_BACKSPACE: return 8;
        case SDLK_TAB: return 9;
        case SDLK_LEFT: return 37;
        case SDLK_UP: return 38;
        case SDLK_RIGHT: return 39;
        case SDLK_DOWN: return 40;
        case SDLK_HOME: return 36;
        case SDLK_END: return 35;
        case SDLK_PAGEUP: return 33;
        case SDLK_PAGEDOWN: return 34;
        case SDLK_INSERT: return 45;
        case SDLK_DELETE: return 46;
        default: break;
    }
    if (sc == SDL_SCANCODE_LCTRL || sc == SDL_SCANCODE_RCTRL) return 17;
    if (sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT) return 16;
    if (sc == SDL_SCANCODE_LALT || sc == SDL_SCANCODE_RALT) return 18;
    return 0;
}

int sdl_button_to_vk(Uint8 button)
{
    return button == SDL_BUTTON_LEFT     ? 1
           : button == SDL_BUTTON_RIGHT  ? 2
           : button == SDL_BUTTON_MIDDLE ? 3
                                         : 0;
}

void touch_tap(HostState* host, int vk, float x, float y)
{
    host->app->mouse_move(x, y);
    host->app->mouse_button(vk, true);
    host->pending_button = Uint8(vk);
    host->pending_x = x;
    host->pending_y = y;
}

void touch_handle(HostState* host, const SDL_Event& ev)
{
    if (!host->window || !host->app) return;
    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(host->window, &win_w, &win_h);
    if (win_w <= 0 || win_h <= 0) return;

    const float wx = ev.tfinger.x * float(win_w);
    const float wy = ev.tfinger.y * float(win_h);

    if (ev.type == SDL_EVENT_FINGER_DOWN) {
        TouchFinger f;
        f.x = f.start_x = wx;
        f.y = f.start_y = wy;
        host->fingers[ev.tfinger.fingerID] = f;
        if (host->fingers.size() == 1)
            host->touch_phase = TouchPhase::SingleFinger;
        else if (host->fingers.size() == 2)
            host->touch_phase = TouchPhase::MultiFinger;
        else
            host->touch_phase = TouchPhase::Idle;
        return;
    }

    if (ev.type == SDL_EVENT_FINGER_MOTION) {
        auto it = host->fingers.find(ev.tfinger.fingerID);
        if (it == host->fingers.end()) return;
        TouchFinger& f = it->second;
        f.x = wx;
        f.y = wy;
        const float dx = wx - f.start_x;
        const float dy = wy - f.start_y;
        constexpr float kMoveThreshold = 12.0f;
        if (dx * dx + dy * dy > kMoveThreshold * kMoveThreshold) f.moved = true;
        if (host->touch_phase == TouchPhase::SingleFinger)
            host->app->mouse_move(f.x, f.y);
        return;
    }

    if (ev.type == SDL_EVENT_FINGER_UP) {
        auto it = host->fingers.find(ev.tfinger.fingerID);
        if (it == host->fingers.end()) return;
        const TouchFinger f = it->second;
        const int remaining = int(host->fingers.size()) - 1;
        host->fingers.erase(it);
        if (remaining > 0) return;

        // One finger = left click, two fingers = right click. Drag is not a tap.
        if (host->touch_phase == TouchPhase::MultiFinger && !f.moved)
            touch_tap(host, 2, f.x, f.y);
        else if (host->touch_phase == TouchPhase::SingleFinger && !f.moved)
            touch_tap(host, 1, f.x, f.y);
        host->touch_phase = TouchPhase::Idle;
    }
}

void touch_flush_release(HostState* host)
{
    if (!host->pending_button || !host->app) return;
    host->app->mouse_button(int(host->pending_button), false);
    host->pending_button = 0;
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    oa::runtime::AppConfig cfg;
    const int parsed = parse_args(argc, argv, cfg);
    if (parsed == 1) return SDL_APP_SUCCESS;
    if (parsed == 2) return SDL_APP_FAILURE;
    std::string title = "openartemis";
#if defined(__APPLE__)
    // A per-game .app: no arguments, the game comes from the bundle. A filled
    // project here means the user quit at the missing-disk prompt.
    if (cfg.project.empty() && !oa::macos::load_bundle(cfg, title) && !cfg.project.empty())
        return SDL_APP_SUCCESS;
#endif
    if (cfg.project.empty()) {
        std::fprintf(stderr, "openartemis: no project given\n");
        print_usage(argv[0]);
        return SDL_APP_FAILURE;
    }

    auto* host = new HostState();
    *appstate = host;

    SDL_SetHintWithPriority(SDL_HINT_MAIN_CALLBACK_RATE, "0", SDL_HINT_OVERRIDE);
    const bool headless = cfg.headless;
    if (!headless) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return SDL_APP_FAILURE;
        }
        host->sdl_inited = true;
    }
    oa::plat::init();
    host->plat_inited = true;

    if (const char* root = std::getenv("OA_SAVE_ROOT"); root && *root) {
        cfg.save_root = root;
    } else if (cfg.save_root.empty()) {
        std::error_code ec;
        const bool is_dir = std::filesystem::is_directory(cfg.project, ec);
        cfg.save_root = oa::plat::default_save_root(cfg.project, is_dir);
    }
    const std::string renderer = cfg.renderer;
#if defined(OA_DRIVE)
    cfg.dialog = oa::drive::dialog; // the script answers
#elif defined(__APPLE__)
    cfg.dialog = [host](const std::string& title, const std::string& message, bool yes_no) {
        return show_dialog(host->window, title, message, yes_no);
    };
#endif

    host->app = new oa::runtime::App(std::move(cfg));
    if (!host->app->boot()) return SDL_APP_FAILURE;
    if (headless) {
        const oa::runtime::AppResult r = host->app->run_headless();
        return r == oa::runtime::AppResult::Error ? SDL_APP_FAILURE
                                                  : SDL_APP_SUCCESS;
    }

    Uint32 flags = SDL_WINDOW_RESIZABLE;
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || defined(__IPHONEOS__)
    flags = (flags | SDL_WINDOW_FULLSCREEN) & ~SDL_WINDOW_RESIZABLE;
#endif
    if (renderer == "gles") flags |= SDL_WINDOW_OPENGL;

    host->window = SDL_CreateWindow(title.c_str(), host->app->stage_width(),
                                    host->app->stage_height(), flags);
    if (!host->window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
#if defined(__ANDROID__) || defined(__EMSCRIPTEN__) || defined(__IPHONEOS__)
    if (!SDL_SetWindowFullscreen(host->window, true))
        std::fprintf(stderr, "[app] SDL_SetWindowFullscreen failed: %s\n",
                     SDL_GetError());
#else
    // Resizing keeps the stage's aspect ratio; fullscreen letterboxes it.
    const float aspect = float(host->app->stage_width()) / float(host->app->stage_height());
    SDL_SetWindowAspectRatio(host->window, aspect, aspect);
#endif
    if (!host->app->attach_window(host->window)) return SDL_APP_FAILURE;
#if defined(__APPLE__)
    tick_through_window_animations(host);
#endif
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* ev)
{
    HostState* host = static_cast<HostState*>(appstate);
    if (!host || !host->app) return SDL_APP_CONTINUE;

#if defined(__APPLE__)
    // ⌘Q and the close button reach the game's own close handler first, as on
    // Windows: it asks, saves (read text, settings) and exits by itself.
    if (ev->type == SDL_EVENT_QUIT && host->app->window_close()) return SDL_APP_CONTINUE;
#endif
    if (ev->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
#if defined(__APPLE__)
    // macOS: Esc belongs to the game (close menu / hide the message window),
    // quitting is ⌘Q. The system takes the function keys, so ⌘1..⌘9 / ⌘0
    // stand for F1..F10, ⌘S / ⌘L for F4 / F5 (quick save / load), and ⌃⌘F
    // toggles fullscreen.
    if (ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP) {
        const bool down = ev->type == SDL_EVENT_KEY_DOWN;
        const SDL_Keycode key = ev->key.key;
        if (key == SDLK_ESCAPE) {
            if (!ev->key.repeat) host->app->key_event(27, down);
            return SDL_APP_CONTINUE;
        }
        if (down && (ev->key.mod & SDL_KMOD_GUI) && (ev->key.mod & SDL_KMOD_CTRL) &&
            key == SDLK_F) {
            if (!ev->key.repeat) set_fullscreen(host, !host->fullscreen);
            return SDL_APP_CONTINUE;
        }
        const int vk = key == SDLK_0                   ? 121
                       : key >= SDLK_1 && key <= SDLK_9 ? 112 + int(key - SDLK_1)
                       : key == SDLK_S                  ? 115
                       : key == SDLK_L                  ? 116
                                                        : 0;
        if (down && (ev->key.mod & SDL_KMOD_GUI) && vk) {
            if (!ev->key.repeat) {
                host->command_keys[key] = vk;
                host->app->key_event(vk, true);
            }
            return SDL_APP_CONTINUE;
        }
        if (!down) {
            auto it = host->command_keys.find(key);
            if (it != host->command_keys.end()) {
                host->app->key_event(it->second, false);
                host->command_keys.erase(it);
                return SDL_APP_CONTINUE;
            }
        }
    }
#else
    if (ev->type == SDL_EVENT_KEY_DOWN && ev->key.key == SDLK_ESCAPE)
        return SDL_APP_SUCCESS;
#endif

    if (ev->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
        ev->type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
        const bool full = ev->type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        std::printf("[app] window %s fullscreen\n", full ? "entered" : "left");
        host->fullscreen = full;
        host->app->set_fullscreen(full);
        host->app->request_repaint();
        return SDL_APP_CONTINUE;
    }
    if (ev->type == SDL_EVENT_WINDOW_RESIZED ||
        ev->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
        ev->type == SDL_EVENT_WINDOW_EXPOSED) {
        host->app->request_repaint();
        return SDL_APP_CONTINUE;
    }

    if (ev->type == SDL_EVENT_FINGER_DOWN || ev->type == SDL_EVENT_FINGER_MOTION ||
        ev->type == SDL_EVENT_FINGER_UP) {
        touch_handle(host, *ev);
        return SDL_APP_CONTINUE;
    }

    if (ev->type == SDL_EVENT_KEY_DOWN || ev->type == SDL_EVENT_KEY_UP) {
        const int vk = sdl_key_to_vk(ev->key.key);
        if (vk > 0) host->app->key_event(vk, ev->type == SDL_EVENT_KEY_DOWN);
    } else if (ev->type == SDL_EVENT_MOUSE_WHEEL) {
        host->app->mouse_wheel(ev->wheel.y > 0 ? 1 : (ev->wheel.y < 0 ? -1 : 0));
    } else if (ev->type == SDL_EVENT_MOUSE_MOTION) {
        host->app->mouse_move(ev->motion.x, ev->motion.y);
    } else if (ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
               ev->type == SDL_EVENT_MOUSE_BUTTON_UP) {
        const int vk = sdl_button_to_vk(ev->button.button);
        if (vk > 0)
            host->app->mouse_button(vk, ev->type == SDL_EVENT_MOUSE_BUTTON_DOWN);
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    HostState* host = static_cast<HostState*>(appstate);
    if (!host || !host->app) return SDL_APP_SUCCESS;
    if (host->ticking) return SDL_APP_CONTINUE;

    if (oa::plat::take_lifecycle_resume()) host->app->reset_clock();
#if defined(OA_DRIVE)
    if (!oa::drive::step(*host->app, host->window, [host](bool on) { set_fullscreen(host, on); }))
        return SDL_APP_SUCCESS;
#endif

    if (host->app->take_fullscreen_toggle()) set_fullscreen(host, !host->fullscreen);

    host->ticking = true;
#if defined(__APPLE__)
    const oa::runtime::AppResult result = host->animation_result != oa::runtime::AppResult::Continue
                                              ? host->animation_result
                                              : host->app->tick();
#else
    const oa::runtime::AppResult result = host->app->tick();
#endif
    host->ticking = false;
    switch (result) {
        case oa::runtime::AppResult::Continue:
            touch_flush_release(host);
            return SDL_APP_CONTINUE;
        case oa::runtime::AppResult::Exit:
            return SDL_APP_SUCCESS;
        case oa::runtime::AppResult::Error:
            return SDL_APP_FAILURE;
    }
    return SDL_APP_FAILURE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    (void)result;
    HostState* host = static_cast<HostState*>(appstate);
    if (!host) return;

#if defined(__APPLE__)
    if (host->animation_timer) {
        CFRunLoopTimerInvalidate(host->animation_timer);
        CFRelease(host->animation_timer);
    }
#endif
    delete host->app;
    if (host->window) SDL_DestroyWindow(host->window);
    if (host->plat_inited) oa::plat::shutdown();
    if (host->sdl_inited) SDL_Quit();
    delete host;
}
