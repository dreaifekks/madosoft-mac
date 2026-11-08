#include "macos_bundle.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_messagebox.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace oa::macos {

namespace {

bool all_present(const oa::runtime::AppConfig& cfg)
{
    std::error_code ec;
    if (!std::filesystem::exists(cfg.project, ec)) return false;
    for (const std::string& o : cfg.overlays)
        if (!std::filesystem::exists(o, ec)) return false;
    return true;
}

} // namespace

bool load_bundle(oa::runtime::AppConfig& cfg, std::string& title)
{
    const char* base = SDL_GetBasePath();
    if (!base) return false;
    std::ifstream in(std::filesystem::path(base) / "game.conf");
    if (!in) return false;

    std::string save;
    for (std::string line; std::getline(in, line);) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "title") title = value;
        else if (key == "project") cfg.project = value;
        else if (key == "overlay") cfg.overlays.push_back(value);
        else if (key == "save") save = value;
    }
    if (cfg.project.empty()) return false;

    const char* home = std::getenv("HOME");
    if (!save.empty() && home)
        cfg.save_root = (std::filesystem::path(home) / "Library/Application Support" / save).string();

    while (!all_present(cfg)) {
        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "重试"},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "退出"},
        };
        const std::string message = "找不到游戏数据，请接上游戏所在的磁盘后重试。\n\n" + cfg.project;
        const SDL_MessageBoxData box = {SDL_MESSAGEBOX_WARNING, nullptr, title.c_str(),
                                        message.c_str(), 2, buttons, nullptr};
        int choice = 0;
        if (!SDL_ShowMessageBox(&box, &choice) || choice != 1) return false;
    }
    return true;
}

} // namespace oa::macos
