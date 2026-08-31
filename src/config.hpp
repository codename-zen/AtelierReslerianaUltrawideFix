#pragma once
#include <filesystem>
#include <string>

struct Config {
    // [General] Master switch. False leaves the plugin loaded but installs
    // nothing, so the game runs exactly as it would without it. Turning off
    // individual sections instead is a trap: a NudgeElements offset is
    // calibrated for the widened canvas, so it misplaces the panel the moment
    // the resolution goes back to 16:9.
    bool enabled = true;

    // [Resolution] 0x0 means "use the primary monitor's desktop resolution".
    bool force_resolution = true;
    int width = 0;
    int height = 0;
    // Unity FullScreenMode: 0 exclusive, 1 borderless, 2 maximised, 3 windowed.
    int fullscreen_mode = 1;

    // [AspectRatio]
    bool unlock_camera_aspect = true;
    bool remove_viewport_letterbox = true;
    // A camera drawing into a screen-sized render texture, shown in a
    // fixed-size UI slot, hands the whole texture to that slot. A wider screen
    // therefore squeezes what it drew. Rendering at the locked aspect instead
    // pre-stretches it by exactly what the slot then squeezes out.
    bool correct_render_texture_aspect = true;

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
    // "viewport" narrows the UI camera: clean and exact, but a full-screen menu
    // then has nothing to draw at the sides.
    // "anchors" leaves the camera full width, so the menu backdrop covers the
    // whole screen, and pushes the HUD into a centred band by remapping the
    // horizontal anchors of each canvas's direct children instead.
    // "off" disables both.
    std::string hud_mode = "off";
    // Whether nested canvases get their children remapped too. A nested canvas
    // is itself a child of the root, so in principle moving the root's children
    // is enough -- but only if the chain is actually anchored rather than sized
    // outright, which is what this exists to test.
    bool anchor_nested_canvases = false;
    // Per-element horizontal nudges, as "Name:offset" entries in canvas units.
    // For layout the game itself gets wrong at a wide aspect, where a general
    // rule would do more harm than a targeted shift.
    std::string nudge_elements;
    // Canvases left full width in anchors mode: the backdrop and blur layers,
    // which are the whole point of keeping the camera wide.
    std::string background_canvas_names =
        "CaptureCanvas,Null_blur,Null_bg,99_cmn_blur_black,FadeCanvas,TransitionCanvas";
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
