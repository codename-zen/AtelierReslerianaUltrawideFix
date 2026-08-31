#include "config.hpp"

#include <windows.h>
#include <fstream>

#include <inipp.h>

#include "log.hpp"

Config g_config;

namespace {

// inipp only exposes extract(); look the key up ourselves so a malformed
// value is reported instead of silently falling back to the default.
template <typename T>
void read(inipp::Ini<char>& ini, const char* section, const char* key, T& value) {
    const auto section_it = ini.sections.find(section);
    if (section_it == ini.sections.end())
        return;

    const auto key_it = section_it->second.find(key);
    if (key_it == section_it->second.end())
        return;

    if (!inipp::extract(key_it->second, value))
        LOG_WARN("Could not parse [{}] {} = {}", section, key, key_it->second);
}

} // namespace

int Config::target_width() const {
    return width > 0 ? width : GetSystemMetrics(SM_CXSCREEN);
}

int Config::target_height() const {
    return height > 0 ? height : GetSystemMetrics(SM_CYSCREEN);
}

void load_config(const std::filesystem::path& ini_path) {
    std::ifstream stream(ini_path);
    if (!stream) {
        LOG_WARN("{} not found, using defaults", ini_path.filename().string());
        return;
    }

    inipp::Ini<char> ini;
    ini.parse(stream);

    read(ini, "General", "Enabled", g_config.enabled);

    read(ini, "Resolution", "Enabled", g_config.force_resolution);
    read(ini, "Resolution", "Width", g_config.width);
    read(ini, "Resolution", "Height", g_config.height);
    read(ini, "Resolution", "FullscreenMode", g_config.fullscreen_mode);

    read(ini, "AspectRatio", "UnlockCameraAspect", g_config.unlock_camera_aspect);
    read(ini, "AspectRatio", "RemoveViewportLetterbox", g_config.remove_viewport_letterbox);
    read(ini, "AspectRatio", "CorrectRenderTextureAspect",
         g_config.correct_render_texture_aspect);

    read(ini, "FOV", "AdditionalVerticalFOV", g_config.additional_vertical_fov);
    read(ini, "FOV", "CorrectVertMinusFOV", g_config.correct_vert_minus_fov);

    read(ini, "HUD", "FixCanvasScaler", g_config.fix_canvas_scaler);
    read(ini, "HUD", "LockAspect", g_config.lock_hud_aspect);
    read(ini, "HUD", "SpoofScreenWidth", g_config.spoof_screen_width);
    read(ini, "HUD", "CameraNames", g_config.hud_camera_names);
    read(ini, "HUD", "SceneCameraNames", g_config.scene_camera_names);
    read(ini, "HUD", "PinBaseCameras", g_config.pin_base_cameras);
    read(ini, "HUD", "Mode", g_config.hud_mode);
    read(ini, "HUD", "AnchorNestedCanvases", g_config.anchor_nested_canvases);
    read(ini, "HUD", "NudgeElements", g_config.nudge_elements);
    read(ini, "HUD", "ConstrainElements", g_config.constrain_elements);
    read(ini, "HUD", "BackgroundCanvases", g_config.background_canvas_names);
    read(ini, "HUD", "RelaxBlackClear", g_config.relax_black_clear);

    read(ini, "Diagnostics", "EnableLogging", g_config.enable_logging);
    read(ini, "Diagnostics", "LogCameras", g_config.log_cameras);
}
