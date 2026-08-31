#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "il2cpp_api.hpp"

namespace unity {

// UnityEngine.Rect -- 16 bytes, so the x64 ABI passes it by hidden pointer.
struct Rect {
    float x, y, width, height;
};

// UnityEngine.UI.CanvasScaler.ScaleMode
enum ScaleMode : int { ConstantPixelSize = 0, ScaleWithScreenSize = 1, ConstantPhysicalSize = 2 };
// UnityEngine.UI.CanvasScaler.ScreenMatchMode
enum ScreenMatchMode : int { MatchWidthOrHeight = 0, Expand = 1, Shrink = 2 };
// UnityEngine.Resolution: two ints followed by a RefreshRate of two uints.
struct Resolution {
    int width, height;
    unsigned int refresh_numerator, refresh_denominator;
};

// UnityEngine.RenderMode
enum RenderMode : int { ScreenSpaceOverlay = 0, ScreenSpaceCamera = 1, WorldSpace = 2 };

struct Vector2 {
    float x, y;
};

struct Bindings {
    // Native entry points we inline-hook.
    void* camera_set_aspect = nullptr;
    void* camera_set_rect = nullptr;
    const MethodInfo* camera_set_rect_info = nullptr;
    void* camera_set_field_of_view = nullptr;
    void* canvas_scaler_handle = nullptr;
    void* time_get_delta_time = nullptr;
    void* screen_get_width = nullptr;
    const MethodInfo* screen_get_width_info = nullptr;
    void* screen_get_safe_area = nullptr;
    void* screen_get_current_resolution = nullptr;
    void* canvas_set_scale_factor = nullptr;
    void* display_get_rendering_width = nullptr;
    void* display_get_system_width = nullptr;

    // CanvasScaler instance field offsets, for direct reads/writes.
    size_t scaler_ui_scale_mode = 0;
    size_t scaler_screen_match_mode = 0;
    size_t scaler_match_width_or_height = 0;
    size_t scaler_reference_resolution = 0;
    size_t scaler_canvas = 0;

    bool canvas_scaler_available = false;
};

// Resolves every class, method and field by name through the il2cpp runtime.
bool resolve();
const Bindings& bindings();

int screen_width();
// The true backbuffer width, even while the game is being told a 16:9 one.
int real_screen_width();
Rect screen_safe_area();
Resolution screen_current_resolution();

// Other places a Unity title can learn the screen shape from.
int display_rendering_width();
int display_rendering_height();
int main_camera_pixel_width();
float main_camera_aspect();
void set_real_screen_width(int width);
int screen_height();
float screen_aspect();
void set_resolution(int width, int height, int fullscreen_mode);

// Cameras that render to a texture (portraits, minimaps, reflections) own their
// aspect legitimately and must be left alone.
bool camera_has_target_texture(void* camera);

// Canvas inspection, used to work out how the HUD can be constrained.
int canvas_render_mode(void* canvas);
bool canvas_has_world_camera(void* canvas);
void* canvas_world_camera(void* canvas);
std::string object_name(void* unity_object);
std::string type_name(void* unity_object);
Rect camera_rect(void* camera);
bool camera_is_orthographic(void* camera);
float camera_aspect(void* camera);
// Aspect of the camera's viewport, which is what its projection should match.
float camera_viewport_aspect(void* camera);
void camera_reset_aspect(void* camera);
// Size of the camera's render target, 0x0 when it draws straight to screen.
void camera_target_texture_size(void* camera, int& width, int& height);
float camera_orthographic_size(void* camera);
// Enough of the camera's render setup to reason about what draws where.
int camera_clear_flags(void* camera);
void set_camera_clear_flags(void* camera, int flags);
float camera_depth(void* camera);
int camera_culling_mask(void* camera);
bool behaviour_enabled(void* behaviour);
// Whether a component's GameObject is live in the scene. Menus the game is not
// showing stay instantiated but inactive, so this is what keeps a walk off them.
bool game_object_active(void* component);
// URP CameraRenderType: 0 Base, 1 Overlay. -1 when unavailable. An overlay
// renders into its base camera's viewport and ignores its own.
int camera_render_type(void* camera);

// Size of a canvas's own RectTransform, in its local units.
Rect canvas_rect(void* canvas);
float canvas_scale_factor(void* canvas);
bool canvas_is_root(void* canvas);
// The canvas at the top of this one's chain, which is the only place a remap
// can be applied without compounding down the tree.
void* canvas_root(void* canvas);
void set_canvas_scale_factor(void* canvas, float value);

// Child traversal, for checking whether a container inside the canvas is still
// sized for the full ultrawide screen.
void* component_transform(void* component);
int transform_child_count(void* transform);
void* transform_child(void* transform, int index);
Rect transform_rect(void* transform);

// RectTransform anchors. Vector2 is 8 bytes, so the x64 ABI moves it through an
// integer register rather than XMM, both in and out.
Vector2 anchor_min(void* rect_transform);
Vector2 anchor_max(void* rect_transform);
void set_anchor_min(void* rect_transform, Vector2 value);
void set_anchor_max(void* rect_transform, Vector2 value);
Vector2 anchored_position(void* rect_transform);
void set_anchored_position(void* rect_transform, Vector2 value);

// Every live instance, including inactive ones. The game's own UI turned out
// not to use a stock CanvasScaler, so enumeration is the only way to find it.
std::vector<void*> find_all_canvases();
std::vector<void*> find_all_cameras();

} // namespace unity
