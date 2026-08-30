#pragma once
#include <filesystem>
#include <string>

struct Config {
    // [Resolution] 0x0 means "use the primary monitor's desktop resolution".
    bool force_resolution = true;
    int width = 0;
    int height = 0;
    // Unity FullScreenMode: 0 exclusive, 1 borderless, 2 maximised, 3 windowed.
    int fullscreen_mode = 1;

    // [AspectRatio]
    bool unlock_camera_aspect = true;
    bool remove_viewport_letterbox = true;

    // [FOV]
    float additional_vertical_fov = 0.0f;
    // Only needed if testing shows the game shrinks vertical FOV as the screen
    // widens (Vert-). Unity is Hor+ by default, so this stays off.
    bool correct_vert_minus_fov = false;

    // [HUD]
    // The game's own canvases already scale by height, so this changes nothing
    // on them; kept for canvases that scale by width.
    bool fix_canvas_scaler = false;
    // Constrain the UI to this aspect, centred, instead of letting it spread to
    // the screen edges. 0 disables.
    float lock_hud_aspect = 16.0f / 9.0f;
    // Report a 16:9 width to the game so its own layout code builds a 16:9 UI.
    // Without this the game keeps sizing containers from the real screen and
    // the viewport only crops an ultrawide layout.
    bool spoof_screen_width = false;
    // Cameras to pin to the locked aspect, by name. Canvas-referenced cameras
    // are handled anyway; this catches the ones that only draw world-space UI,
    // such as the battle HUD.
    std::string hud_camera_names = "UIDummyCamera,UIEffectDummyCamera,EventCamera";
    // Never narrowed, whatever else matches: these draw the 3D scene, which has
    // to keep the full width.
    std::string scene_camera_names = "Main Camera";
    // A URP base camera decides the final image area, so narrowing one confines
    // everything behind the UI as well and leaves the sides black. The UI is
    // drawn by an overlay camera, and narrowing only that still shrinks the
    // canvas. Set true to go back to narrowing base cameras too.
    bool pin_base_cameras = false;
    // Relaxes the clear on a full-screen camera that draws nothing. Tried
    // against the black sides of in-game menus and it changed nothing: the
    // game re-applies the clear every frame, and there is no scene rendered
    // out there to reveal anyway. Off by default; stopping a clear risks
    // smearing for no gain.
    bool relax_black_clear = false;

    // [Diagnostics]
    bool enable_logging = true;
    bool log_cameras = false;

    // Resolved from width/height, or from the desktop when those are 0.
    int target_width() const;
    int target_height() const;
};

extern Config g_config;

void load_config(const std::filesystem::path& ini_path);
