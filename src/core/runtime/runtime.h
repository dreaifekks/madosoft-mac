#pragma once
// Host contract: the SDL layer only starts the engine, feeds input, advances
// frames and shuts down. Engine internals live in runtime_game.h.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace oa::runtime {

/// Host modal message box for [dialog] (the native engine's OS dialog):
/// yes/no when the script wants an answer, else OK. Returns 1 = yes/OK, 0 = no.
using DialogHost = std::function<int(const std::string& title, const std::string& message,
                                     bool yes_no)>;

/// Host boot settings (CLI parsing and platform policy stay in main.cpp).
struct AppConfig {
    std::string project;               // .pfs archive or extracted project dir
    std::vector<std::string> overlays; // .pfs files / dirs mounted above it (last wins)
    std::string save_root;             // host-chosen writable save directory
    std::string platform = "windows";  // system.ini section / script os var
    std::string renderer = "sdl";      // "sdl" or "gles"
    std::string dump;                  // end-of-run PPM path (--dump)
    int fps = 0;                       // 0 = project fps + timer delay
    int fixed_delta_ms = 0;            // > 0 = deterministic tick step
    uint64_t frames_target = 0;        // > 0 = exit after N frames
    bool headless = false;             // no window/renderer
    bool mute = false;
    DialogHost dialog;                 // unset = [dialog] stays a no-op
};

enum class AppResult { Continue, Exit, Error };

/// Engine host: runtime + renderer + frame loop. No SDL types in this API;
/// main.cpp converts SDL events before calling the input methods.
class App {
public:
    explicit App(AppConfig config);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool boot();
    bool attach_window(void* native_window);
    int stage_width() const;
    int stage_height() const;
    AppResult run_headless();
    void reset_clock();
    void request_repaint();
    /// Write the last presented frame as PPM (test drivers).
    void dump(const std::string& path);
    /// Print the OA_SCENEDUMP / OA_TXTDUMP / OA_VARDUMP reports now.
    void dump_state();

    void key_event(int vk, bool down);
    void mouse_move(float window_x, float window_y);
    void mouse_button(int vk, bool down); // 1 left, 2 right, 3 middle
    void mouse_wheel(int direction);      // > 0 up, < 0 down
    /// Quit request (close button, ⌘Q): true when the game's own close
    /// handler takes it (it asks, saves and exits); false = quit now.
    bool window_close();
    /// Window state the scripts read back ([var system=fullscreen]).
    void set_fullscreen(bool on);
    /// True once per script [exec command=fullscreen]: the host toggles.
    bool take_fullscreen_toggle();

    /// One frame. pace = false skips the frame-rate sleep, for a host timer
    /// that already sets the rate.
    AppResult tick(bool pace = true);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace oa::runtime
