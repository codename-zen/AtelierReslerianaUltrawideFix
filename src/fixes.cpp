#include "fixes.hpp"

#include <windows.h>

#include <cmath>
#include <string>
#include <unordered_set>

#include <safetyhook.hpp>

#include "anchors.hpp"
#include "config.hpp"
#include "elements.hpp"
#include "log.hpp"
#include "unity_bindings.hpp"

namespace fixes {
namespace {

constexpr float kReferenceAspect = 16.0f / 9.0f;
constexpr float kDegToRad = 0.01745329252f;
constexpr float kRadToDeg = 57.2957795131f;
// The game re-applies its own resolution when the options screen is confirmed.
// Give up after a few rounds instead of fighting it forever.
constexpr int kMaxResolutionAttempts = 10;

SafetyHookInline g_set_aspect{};
SafetyHookInline g_set_rect{};
SafetyHookInline g_set_field_of_view{};
SafetyHookInline g_canvas_scaler_handle{};
SafetyHookInline g_get_delta_time{};
SafetyHookInline g_screen_get_width{};
SafetyHookInline g_screen_get_safe_area{};
SafetyHookInline g_screen_get_current_resolution{};
SafetyHookInline g_display_get_rendering_width{};
SafetyHookInline g_display_get_system_width{};

// Touched only from the Unity main thread, inside the hooks below.
ULONGLONG g_last_resolution_check = 0;
int g_resolution_attempts = 0;

// A centred band that trims exactly one axis is the classic letterbox or
// pillarbox. Off-centre or small viewports belong to minimaps, character
// portraits and split-screen effects, so they are left untouched.
bool is_letterbox(const unity::Rect& rect) {
    const bool full_width = rect.width > 0.999f && std::fabs(rect.x) < 0.001f;
    const bool full_height = rect.height > 0.999f && std::fabs(rect.y) < 0.001f;
    const bool trims_width =
        rect.width < 0.999f && std::fabs(rect.x - (1.0f - rect.width) * 0.5f) < 0.01f;
    const bool trims_height =
        rect.height < 0.999f && std::fabs(rect.y - (1.0f - rect.height) * 0.5f) < 0.01f;
    return (trims_width && full_height) || (trims_height && full_width);
}

void enforce_resolution() {
    if (!g_config.force_resolution || g_resolution_attempts >= kMaxResolutionAttempts)
        return;

    const ULONGLONG now = GetTickCount64();
    if (now - g_last_resolution_check < 1000)
        return;
    g_last_resolution_check = now;

    const int target_width = g_config.target_width();
    const int target_height = g_config.target_height();
    if (unity::real_screen_width() == target_width && unity::screen_height() == target_height) {
        g_resolution_attempts = 0;
        return;
    }

    ++g_resolution_attempts;
    LOG_INFO("Applying {}x{} (fullscreen mode {}), was {}x{}", target_width, target_height,
             g_config.fullscreen_mode, unity::real_screen_width(), unity::screen_height());
    unity::set_resolution(target_width, target_height, g_config.fullscreen_mode);

    if (g_resolution_attempts >= kMaxResolutionAttempts)
        LOG_WARN("Resolution kept reverting; leaving it to the game from now on.");
}

// UnityEngine.Camera::set_aspect(float)
void set_aspect_hook(void* self, float value, const MethodInfo* method) {
    if (g_config.unlock_camera_aspect && !unity::camera_has_target_texture(self)) {
        // The right aspect is the camera's own viewport, not the whole screen.
        // Using the screen would squash everything drawn by a camera we have
        // narrowed to a 16:9 band.
        const float actual = unity::camera_viewport_aspect(self);
        if (actual > 0.0f && std::fabs(actual - value) > 0.001f) {
            if (g_config.log_cameras)
                LOG_INFO("Camera::set_aspect {:.4f} -> {:.4f}", value, actual);
            value = actual;
        }
    }
    g_set_aspect.unsafe_call<void>(self, value, method);
}

// UnityEngine.Camera::set_rect(Rect). Rect is 16 bytes, so the x64 ABI hands it
// over as a pointer to a caller-owned temporary rather than in registers.
void set_rect_hook(void* self, unity::Rect* value, const MethodInfo* method) {
    unity::Rect rect = *value;
    if (g_config.remove_viewport_letterbox && !unity::camera_has_target_texture(self) &&
        is_letterbox(rect)) {
        LOG_INFO("Camera::set_rect letterbox ({:.3f},{:.3f},{:.3f},{:.3f}) -> full viewport", rect.x,
                 rect.y, rect.width, rect.height);
        rect = unity::Rect{0.0f, 0.0f, 1.0f, 1.0f};
    }
    g_set_rect.unsafe_call<void>(self, &rect, method);
}

// UnityEngine.Camera::set_fieldOfView(float)
void set_field_of_view_hook(void* self, float value, const MethodInfo* method) {
    float fov = value;

    if (g_config.correct_vert_minus_fov) {
        const float aspect = unity::screen_aspect();
        if (aspect > kReferenceAspect) {
            // Undo a Vert- projection: recover the vertical FOV the 16:9 layout
            // would have used, which is what Hor+ needs at a wider aspect.
            const float half = std::tan(fov * kDegToRad * 0.5f);
            fov = 2.0f * std::atan(half * aspect / kReferenceAspect) * kRadToDeg;
        }
    }

    fov += g_config.additional_vertical_fov;
    fov = std::fmin(std::fmax(fov, 1.0f), 179.0f);

    if (g_config.log_cameras && std::fabs(fov - value) > 0.01f)
        LOG_INFO("Camera::set_fieldOfView {:.2f} -> {:.2f}", value, fov);

    g_set_field_of_view.unsafe_call<void>(self, fov, method);
}

// Reports each canvas once, with the values the game itself set, so the right
// way to constrain the HUD can be chosen from evidence.
void describe_scaler(void* self) {
    static std::unordered_set<void*> reported;
    if (reported.size() > 64 || !reported.insert(self).second)
        return;

    const unity::Bindings& bindings = unity::bindings();
    auto* object = static_cast<char*>(self);
    const int scale_mode = *reinterpret_cast<int*>(object + bindings.scaler_ui_scale_mode);
    const int match_mode = *reinterpret_cast<int*>(object + bindings.scaler_screen_match_mode);
    const float match = *reinterpret_cast<float*>(object + bindings.scaler_match_width_or_height);

    unity::Vector2 reference{0.0f, 0.0f};
    if (bindings.scaler_reference_resolution)
        reference = *reinterpret_cast<unity::Vector2*>(object + bindings.scaler_reference_resolution);

    void* canvas = bindings.scaler_canvas
                       ? *reinterpret_cast<void**>(object + bindings.scaler_canvas)
                       : nullptr;

    LOG_INFO("Canvas '{}': renderMode={} worldCamera={} scaleMode={} matchMode={} match={:.2f} "
             "reference={:.0f}x{:.0f}",
             unity::object_name(self), unity::canvas_render_mode(canvas),
             unity::canvas_has_world_camera(canvas), scale_mode, match_mode, match, reference.x,
             reference.y);
}

// UnityEngine.UI.CanvasScaler::Handle(). Runs every frame per canvas, so the
// values are re-asserted even when the game reloads its own UI settings.
void canvas_scaler_handle_hook(void* self, const MethodInfo* method) {
    const unity::Bindings& bindings = unity::bindings();
    if (self)
        describe_scaler(self);
    if (g_config.fix_canvas_scaler && self) {
        auto* object = static_cast<char*>(self);
        const int scale_mode = *reinterpret_cast<int*>(object + bindings.scaler_ui_scale_mode);
        if (scale_mode == unity::ScaleWithScreenSize) {
            // Match the reference resolution by height. Matching width instead
            // blows the HUD up until it overflows a 21:9 or 32:9 screen.
            *reinterpret_cast<int*>(object + bindings.scaler_screen_match_mode) =
                unity::MatchWidthOrHeight;
            *reinterpret_cast<float*>(object + bindings.scaler_match_width_or_height) = 1.0f;
        }
    }
    g_canvas_scaler_handle.unsafe_call<void>(self, method);
}

// Dumps every canvas and camera in the scene. The game's main UI does not use
// a stock CanvasScaler, so this is how its actual structure gets identified.
// Bound to a key so it can be triggered on the exact screen being diagnosed.
void dump_scene() {
    LOG_INFO("==== scene dump: screen is {}x{} ====", unity::screen_width(),
             unity::screen_height());

    LOG_INFO("-- canvases --");
    for (void* canvas : unity::find_all_canvases()) {
        void* camera = unity::canvas_world_camera(canvas);
        const unity::Rect rect = unity::canvas_rect(canvas);
        LOG_INFO("  '{}' [{}] renderMode={} camera='{}'@{} rect={:.0f}x{:.0f}",
                 unity::object_name(canvas), unity::type_name(canvas),
                 unity::canvas_render_mode(canvas), camera ? unity::object_name(camera) : "<none>",
                 camera, rect.width, rect.height);
    }

    LOG_INFO("-- cameras --");
    for (void* camera : unity::find_all_cameras()) {
        const unity::Rect rect = unity::camera_rect(camera);
        int rt_width = 0;
        int rt_height = 0;
        unity::camera_target_texture_size(camera, rt_width, rt_height);
        // clearFlags: 1 Skybox, 2 SolidColor, 3 Depth, 4 Nothing.
        // renderType: 0 Base, 1 Overlay, -1 unknown.
        LOG_INFO("  '{}'@{} rect=({:.3f},{:.3f},{:.3f},{:.3f}) aspect={:.4f} enabled={} clear={} "
                 "depth={:.1f} cull=0x{:x} rt={}x{} renderType={}",
                 unity::object_name(camera), camera, rect.x, rect.y, rect.width, rect.height,
                 unity::camera_aspect(camera), unity::behaviour_enabled(camera),
                 unity::camera_clear_flags(camera), unity::camera_depth(camera),
                 static_cast<unsigned>(unity::camera_culling_mask(camera)), rt_width, rt_height,
                 unity::camera_render_type(camera));
    }

    LOG_INFO("-- element tree of each root canvas --");
    for (void* canvas : unity::find_all_canvases()) {
        if (unity::canvas_render_mode(canvas) == unity::ScreenSpaceCamera &&
            unity::canvas_is_root(canvas))
            elements::dump(canvas);
    }

    LOG_INFO("==== end of dump ====");
}

// Pins every UI camera to a centred band of the requested aspect. A Screen
// Space - Camera canvas derives its size from its camera's viewport, so
// narrowing the viewport pulls the whole HUD back into a 16:9 box while the 3D
// scene keeps the full width.
// Every live canvas, whatever its render mode, with the root of its chain. The
// render-mode filter below was hiding exactly the canvases that matter: the
// menu hierarchy never appeared because its root is not a Screen Space - Camera
// canvas, so it was skipped before anything could look at it.
void report_canvas_chain(void* canvas, float band) {
    static std::unordered_set<void*> reported;
    if (band > 0.999f || reported.size() > 40 || !reported.insert(canvas).second)
        return;

    void* root = unity::canvas_root(canvas);
    const unity::Rect rect = unity::canvas_rect(canvas);
    void* transform = unity::component_transform(canvas);
    LOG_INFO("Chain: '{}' mode={} root={} rootCanvas='{}' children={} rect={:.0f}x{:.0f}",
             unity::object_name(canvas), unity::canvas_render_mode(canvas),
             unity::canvas_is_root(canvas), root ? unity::object_name(root) : "<none>",
             unity::transform_child_count(transform), rect.width, rect.height);
}

// Logs a canvas and the containers directly inside it, once each. If the canvas
// comes back 16:9 but a child is still as wide as the whole screen, then the
// game lays its UI out itself and constraining the viewport only crops it.
void report_canvas_layout(void* canvas) {
    static std::unordered_set<void*> reported;
    if (reported.size() > 48 || !reported.insert(canvas).second)
        return;

    const unity::Rect rect = unity::canvas_rect(canvas);
    void* camera = unity::canvas_world_camera(canvas);
    // The pointer matters: three separate cameras share the name UIDummyCamera,
    // and telling them apart is what decides whether the background can stay
    // full width while the HUD is constrained.
    LOG_INFO("Canvas '{}' is {:.0f}x{:.0f} (aspect {:.4f}) camera='{}'@{}",
             unity::object_name(canvas), rect.width, rect.height,
             rect.height > 0.0f ? rect.width / rect.height : 0.0f,
             camera ? unity::object_name(camera) : "<none>", camera);

    void* transform = unity::component_transform(canvas);
    const int children = unity::transform_child_count(transform);
    for (int i = 0; i < children && i < 8; ++i) {
        void* child = unity::transform_child(transform, i);
        if (!child)
            continue;
        const unity::Rect child_rect = unity::transform_rect(child);
        LOG_INFO("    child '{}' {:.0f}x{:.0f}", unity::object_name(child), child_rect.width,
                 child_rect.height);
    }
}

// True when `name` appears in a comma separated list.
bool name_in_list(const std::string& name, const std::string& list) {
    if (name.empty())
        return false;

    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(',', start);
        if (end == std::string::npos)
            end = list.size();

        size_t first = list.find_first_not_of(" 	", start);
        size_t last = list.find_last_not_of(" 	", end > start ? end - 1 : start);
        if (first != std::string::npos && last != std::string::npos && first <= last) {
            if (name == list.substr(first, last - first + 1))
                return true;
        }
        start = end + 1;
    }
    return false;
}

bool is_hud_camera_name(const std::string& name) {
    return name_in_list(name, g_config.hud_camera_names);
}

bool is_scene_camera_name(const std::string& name) {
    return name_in_list(name, g_config.scene_camera_names);
}

void pin_camera(void* camera, float left, float fraction, const char* reason) {
    if (!camera || unity::camera_has_target_texture(camera))
        return;

    // The scene camera must keep the whole screen, or the 3D view gets
    // pillarboxed along with the HUD.
    if (is_scene_camera_name(unity::object_name(camera)))
        return;

    // A URP base camera owns the final image area. Narrowing one confines the
    // whole composition -- scene included -- which is what left full-screen
    // menus with black sides. Release it instead, and let the overlay camera
    // that actually draws the UI carry the constraint.
    if (!g_config.pin_base_cameras && unity::camera_render_type(camera) == 0) {
        left = 0.0f;
        fraction = 1.0f;
    }

    const unity::Rect current = unity::camera_rect(camera);
    if (std::fabs(current.x - left) < 0.001f && std::fabs(current.width - fraction) < 0.001f)
        return;

    unity::Rect wanted{left, 0.0f, fraction, 1.0f};
    // Call the original directly: going through our own set_rect detour would
    // treat this as letterboxing and undo it immediately.
    g_set_rect.unsafe_call<void>(camera, &wanted, unity::bindings().camera_set_rect_info);
    // Clears any manual aspect override so the projection follows the new
    // viewport instead of staying at the full screen's aspect.
    unity::camera_reset_aspect(camera);

    LOG_INFO("Pinned camera '{}' ({}) -> ({:.3f},0,{:.3f},1); aspect now {:.4f}",
             unity::object_name(camera), reason, left, fraction, unity::camera_aspect(camera));
}

// Design heights a Unity UI is authored against. The game inflates its canvas
// by (screenAspect / 16:9), turning a 2160 unit design into 2902, so snapping
// the height back to the nearest of these undoes exactly that. Snapping is also
// idempotent: a canvas already at its design height is left alone, which a
// blind multiply-back would not be.
constexpr float kDesignHeights[] = {2160.0f, 1440.0f, 1080.0f, 720.0f};

// The game never calls Canvas::set_scaleFactor for the canvases that matter --
// hooking the setter produced no hits at all -- so the value is written here
// instead, from the periodic pass.
void correct_canvas_scale(void* canvas, void* camera) {
    if (g_config.lock_hud_aspect <= 0.0f || !canvas)
        return;

    const float canvas_height = unity::canvas_rect(canvas).height;
    if (canvas_height <= 1.0f)
        return;

    const unity::Rect viewport =
        camera ? unity::camera_rect(camera) : unity::Rect{0.0f, 0.0f, 1.0f, 1.0f};
    const float pixel_height = static_cast<float>(unity::screen_height()) * viewport.height;
    if (pixel_height <= 1.0f)
        return;

    // Only a root canvas takes its size from the viewport. Nested ones carry
    // their own RectTransform -- the key guide is 2000x120 -- and snapping
    // those to a design height inflated them wildly.
    if (!unity::canvas_is_root(canvas))
        return;

    // Undo only the inflation we can account for: the game's canvas is exactly
    // (design x screenAspect / lockAspect) tall. Anything else is a size the
    // game meant, and is left alone. This also makes the pass idempotent, since
    // a corrected canvas sits at its design height and no longer matches.
    const float inflation = unity::screen_aspect() / g_config.lock_hud_aspect;
    float design = 0.0f;
    for (const float candidate : kDesignHeights) {
        if (std::fabs(canvas_height - candidate * inflation) / (candidate * inflation) < 0.015f) {
            design = candidate;
            break;
        }
    }
    if (design <= 0.0f)
        return;

    const float wanted = pixel_height / design;
    const float current = unity::canvas_scale_factor(canvas);
    if (current > 0.0f && std::fabs(current - wanted) / wanted < 0.005f)
        return;

    // Only a canvas whose rect actually derives from the viewport can be fixed
    // by its scale factor. If height x scale does not come back to the viewport
    // height, the game is sizing this rect itself, and rewriting the scale
    // factor just corrupts it -- which is what happened to the video player
    // canvas, reported as 2160 units tall while carrying a scale factor of
    // 1.3333 that implies 1080.
    if (current > 0.0f && std::fabs(canvas_height * current - pixel_height) / pixel_height > 0.02f)
        return;

    unity::set_canvas_scale_factor(canvas, wanted);

    static int logged = 0;
    if (logged < 16) {
        ++logged;
        LOG_INFO("Canvas '{}' {:.0f} units tall -> design {:.0f}; scaleFactor {:.4f} -> {:.4f}",
                 unity::object_name(canvas), canvas_height, design, current, wanted);
    }
}

// UnityEngine.CameraClearFlags: 1 Skybox, 2 SolidColor, 3 Depth, 4 Nothing.
constexpr int kClearSolidColor = 2;
constexpr int kClearDepth = 3;

// In-game menus keep black sides even with the base cameras released, and the
// dump named the culprit: a full-screen base camera with cullingMask 0, which
// draws nothing at all and exists purely to wipe the frame to black. Relaxing
// that wipe to depth-only leaves whatever the scene camera drew underneath.
void relax_black_clear(void* camera) {
    if (!g_config.relax_black_clear || !camera)
        return;
    if (unity::camera_culling_mask(camera) != 0)
        return;
    if (unity::camera_clear_flags(camera) != kClearSolidColor)
        return;
    if (unity::camera_has_target_texture(camera))
        return;

    const unity::Rect rect = unity::camera_rect(camera);
    if (rect.width < 0.999f || rect.height < 0.999f)
        return;

    unity::set_camera_clear_flags(camera, kClearDepth);

    static int logged = 0;
    if (logged < 8) {
        ++logged;
        LOG_INFO("Camera '{}' draws nothing and cleared to solid colour; clear -> depth only",
                 unity::object_name(camera));
    }
}

// A camera drawing into a screen-sized render texture that is then shown in a
// fixed-size UI slot has everything it drew squeezed by the slot, because the
// whole texture is mapped onto it however wide the texture got. The Party
// screen's live character was 26 percent narrow at 21:9 for exactly this
// reason: 1.7778 / 2.3889 = 0.744.
//
// Rendering at the locked aspect instead pre-stretches the image inside the
// texture by the same factor the slot squeezes out, and the two cancel.
void correct_render_texture_aspect(void* camera) {
    if (!g_config.correct_render_texture_aspect || g_config.lock_hud_aspect <= 0.0f || !camera)
        return;

    int width = 0;
    int height = 0;
    unity::camera_target_texture_size(camera, width, height);
    if (width <= 0 || height <= 0)
        return;

    const float texture_aspect = static_cast<float>(width) / static_cast<float>(height);
    if (texture_aspect <= g_config.lock_hud_aspect + 0.001f)
        return;

    if (std::fabs(unity::camera_aspect(camera) - g_config.lock_hud_aspect) < 0.001f)
        return;

    unity::set_camera_aspect(camera, g_config.lock_hud_aspect);

    static int logged = 0;
    if (logged < 8) {
        ++logged;
        LOG_INFO("Camera '{}' draws into a {}x{} texture shown in a fixed slot; aspect -> {:.4f}",
                 unity::object_name(camera), width, height, g_config.lock_hud_aspect);
    }
}

void enforce_hud_viewport() {
    if (g_config.lock_hud_aspect <= 0.0f || !unity::bindings().camera_set_rect_info)
        return;

    const float screen = unity::screen_aspect();

    // A full viewport when the lock does not apply, so cameras pinned earlier
    // are released again. The game flips back to a 16:9 resolution of its own
    // accord, and leaving them pinned squeezed the UI a second time.
    float fraction = 1.0f;
    if (screen > g_config.lock_hud_aspect + 0.001f)
        fraction = g_config.lock_hud_aspect / screen;

    // Two ways to reach the same 16:9 HUD, and they are mutually exclusive:
    // either the camera is narrowed, or the camera stays wide and the HUD's
    // anchors are pulled in. Narrowing also confines the menu backdrop, which
    // is what leaves the sides black, so the anchor route trades exactness for
    // a full-width backdrop.
    const bool use_anchors = g_config.hud_mode == "anchors";
    const bool use_viewport = g_config.hud_mode == "viewport";

    static ULONGLONG last_nudge_pass = 0;
    const ULONGLONG now = GetTickCount64();
    // A quarter second, not a whole one: at one second the panel visibly jumped
    // into place after a menu opened. The walk can afford it now that it stops
    // as soon as every entry is placed.
    const bool nudge_due = now - last_nudge_pass >= 250;
    if (nudge_due)
        last_nudge_pass = now;

    const float camera_fraction = use_viewport ? fraction : 1.0f;
    const float anchor_band = use_anchors ? fraction : 1.0f;
    const float left = (1.0f - camera_fraction) * 0.5f;

    for (void* canvas : unity::find_all_canvases()) {
        report_canvas_chain(canvas, anchor_band);

        if (unity::canvas_render_mode(canvas) != unity::ScreenSpaceCamera)
            continue;
        report_canvas_layout(canvas);
        void* camera = unity::canvas_world_camera(canvas);
        pin_camera(camera, left, camera_fraction, "canvas");
        correct_canvas_scale(canvas, camera);

        // A nested canvas is itself a child of the root, so moving the root's
        // children should be enough -- unless the chain is sized outright
        // rather than anchored, which the log will show.
        if (unity::canvas_is_root(canvas) || g_config.anchor_nested_canvases)
            anchors::remap(canvas, anchor_band);

        // Independent of the HUD mode: these correct the game's own layout,
        // not ours, so they apply even with the lock off.
        if (nudge_due && unity::canvas_is_root(canvas))
            elements::apply_nudges(canvas, fraction);
    }

    // World-space UI, such as the battle HUD, hangs off cameras no canvas
    // points at, so those are matched by name instead.
    for (void* camera : unity::find_all_cameras()) {
        if (is_hud_camera_name(unity::object_name(camera)))
            pin_camera(camera, left, camera_fraction, "name");
        relax_black_clear(camera);
        correct_render_texture_aspect(camera);
    }
}

void poll_hotkeys() {
    // The low bit of GetAsyncKeyState is set once per fresh press.
    if (GetAsyncKeyState(VK_F10) & 1)
        dump_scene();
}

// UnityEngine.Screen::get_width(). The game sizes its own UI containers from
// this: the dump showed a container of 5160x2160, which is 2160 x the real
// screen aspect. Reporting a 16:9 width makes the game build a 16:9 layout,
// which is what narrowing the viewport alone could never achieve. Our own code
// keeps using real_screen_width(), and the 3D camera is unaffected because
// Unity derives a camera's aspect from its viewport, not from Screen.
int screen_get_width_hook(const MethodInfo* method) {
    const int real = g_screen_get_width.unsafe_call<int>(method);
    unity::set_real_screen_width(real);

    if (!g_config.spoof_screen_width || g_config.lock_hud_aspect <= 0.0f)
        return real;

    const int height = unity::screen_height();
    if (height <= 0)
        return real;

    const int capped = static_cast<int>(std::lround(height * g_config.lock_hud_aspect));
    return capped < real ? capped : real;
}

// The game derives its UI reference resolution from the real screen aspect, and
// it does not read that from Screen.width, so the other places a Unity title
// can learn the screen shape are spoofed too.

// UnityEngine.Screen::get_safeArea() -> Rect, returned through a hidden buffer.
void screen_get_safe_area_hook(unity::Rect* result, const MethodInfo* method) {
    g_screen_get_safe_area.unsafe_call<void>(result, method);
    if (!g_config.spoof_screen_width || g_config.lock_hud_aspect <= 0.0f || !result)
        return;

    const float capped = result->height * g_config.lock_hud_aspect;
    if (capped < result->width) {
        result->x = 0.0f;
        result->width = capped;
    }
}

// UnityEngine.Screen::get_currentResolution() -> Resolution, also by buffer.
void screen_get_current_resolution_hook(unity::Resolution* result, const MethodInfo* method) {
    g_screen_get_current_resolution.unsafe_call<void>(result, method);
    if (!g_config.spoof_screen_width || g_config.lock_hud_aspect <= 0.0f || !result)
        return;

    const int capped = static_cast<int>(std::lround(result->height * g_config.lock_hud_aspect));
    if (capped < result->width)
        result->width = capped;
}

// UnityEngine.Display::get_renderingWidth() / get_systemWidth(). The render
// target size is the remaining place the game could be reading a 21:9 aspect
// from, now that every Screen API reports 16:9.
int display_get_rendering_width_hook(void* self, const MethodInfo* method) {
    const int real = g_display_get_rendering_width.unsafe_call<int>(self, method);
    if (!g_config.spoof_screen_width || g_config.lock_hud_aspect <= 0.0f)
        return real;

    const int height = unity::screen_height();
    if (height <= 0)
        return real;
    const int capped = static_cast<int>(std::lround(height * g_config.lock_hud_aspect));
    return capped < real ? capped : real;
}

int display_get_system_width_hook(void* self, const MethodInfo* method) {
    const int real = g_display_get_system_width.unsafe_call<int>(self, method);
    if (!g_config.spoof_screen_width || g_config.lock_hud_aspect <= 0.0f)
        return real;

    const int height = unity::screen_height();
    if (height <= 0)
        return real;
    const int capped = static_cast<int>(std::lround(height * g_config.lock_hud_aspect));
    return capped < real ? capped : real;
}

// Reports what each source of screen geometry says, so the one the game
// actually believes can be identified rather than guessed at.
void report_screen_sources() {
    // Twice: the first tick lands before the resolution has been forced, so a
    // later sample is what actually reflects the running state.
    static int reports = 0;
    static ULONGLONG first_report = 0;
    if (reports >= 2)
        return;
    const ULONGLONG now = GetTickCount64();
    if (reports == 1 && now - first_report < 8000)
        return;
    if (reports == 0)
        first_report = now;
    ++reports;

    const unity::Rect safe = unity::screen_safe_area();
    const unity::Resolution resolution = unity::screen_current_resolution();
    LOG_INFO("Screen sources: width={} (real {}) height={} safeArea={:.0f}x{:.0f} "
             "currentResolution={}x{} displayRendering={}x{} mainCamera={}px aspect={:.4f}",
             unity::screen_width(), unity::real_screen_width(), unity::screen_height(), safe.width,
             safe.height, resolution.width, resolution.height, unity::display_rendering_width(),
             unity::display_rendering_height(), unity::main_camera_pixel_width(),
             unity::main_camera_aspect());
}

// UnityEngine.Time::get_deltaTime() -- the per-frame main-thread pump.
float get_delta_time_hook(const MethodInfo* method) {
    const float delta = g_get_delta_time.unsafe_call<float>(method);
    poll_hotkeys();
    enforce_resolution();

    // Menus open and close constantly, so this runs far more often than the
    // resolution check, but still not every frame.
    static ULONGLONG last_hud_pass = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - last_hud_pass >= 250) {
        last_hud_pass = now;
        report_screen_sources();
        enforce_hud_viewport();
    }
    return delta;
}

bool create(SafetyHookInline& hook, void* target, void* detour, const char* name) {
    if (!target) {
        LOG_WARN("Skipping hook {}: target not resolved", name);
        return false;
    }
    hook = safetyhook::create_inline(target, detour);
    if (!hook) {
        LOG_ERROR("Failed to hook {}", name);
        return false;
    }
    LOG_INFO("Hooked {} at {}", name, target);
    return true;
}

} // namespace

bool install() {
    const unity::Bindings& bindings = unity::bindings();

    const bool aspect_ok = create(g_set_aspect, bindings.camera_set_aspect,
                                  reinterpret_cast<void*>(&set_aspect_hook), "Camera::set_aspect");
    create(g_set_rect, bindings.camera_set_rect, reinterpret_cast<void*>(&set_rect_hook),
           "Camera::set_rect");
    create(g_set_field_of_view, bindings.camera_set_field_of_view,
           reinterpret_cast<void*>(&set_field_of_view_hook), "Camera::set_fieldOfView");
    create(g_get_delta_time, bindings.time_get_delta_time,
           reinterpret_cast<void*>(&get_delta_time_hook), "Time::get_deltaTime");
    create(g_screen_get_width, bindings.screen_get_width,
           reinterpret_cast<void*>(&screen_get_width_hook), "Screen::get_width");
    create(g_screen_get_safe_area, bindings.screen_get_safe_area,
           reinterpret_cast<void*>(&screen_get_safe_area_hook), "Screen::get_safeArea");
    create(g_screen_get_current_resolution, bindings.screen_get_current_resolution,
           reinterpret_cast<void*>(&screen_get_current_resolution_hook),
           "Screen::get_currentResolution");
    create(g_display_get_rendering_width, bindings.display_get_rendering_width,
           reinterpret_cast<void*>(&display_get_rendering_width_hook),
           "Display::get_renderingWidth");
    create(g_display_get_system_width, bindings.display_get_system_width,
           reinterpret_cast<void*>(&display_get_system_width_hook), "Display::get_systemWidth");

    if (bindings.canvas_scaler_available)
        create(g_canvas_scaler_handle, bindings.canvas_scaler_handle,
               reinterpret_cast<void*>(&canvas_scaler_handle_hook), "CanvasScaler::Handle");
    else
        LOG_WARN("CanvasScaler unavailable; HUD scaling left untouched.");

    return aspect_ok;
}

} // namespace fixes
