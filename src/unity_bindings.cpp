#include "unity_bindings.hpp"

#include "log.hpp"

namespace unity {
namespace {

constexpr const char* kCoreModule = "UnityEngine.CoreModule.dll";
constexpr const char* kUiModule = "UnityEngine.UI.dll";
// UnityEngine.Canvas lives in the engine module, not in the uGUI package.
constexpr const char* kUiEngineModule = "UnityEngine.UIModule.dll";
constexpr const char* kUrpModule = "Unity.RenderPipelines.Universal.Runtime.dll";

Bindings g_bindings;

// A resolved method: its native entry point plus the MethodInfo* that il2cpp
// expects as the trailing argument.
struct Call {
    void* ptr = nullptr;
    const MethodInfo* info = nullptr;
    explicit operator bool() const { return ptr != nullptr; }
};

Call make_call(const MethodInfo* method) {
    return Call{il2cpp::method_pointer(method), method};
}

Call g_screen_width;
Call g_screen_height;
Call g_screen_set_resolution;
Call g_camera_get_target_texture;
Call g_canvas_get_render_mode;
Call g_canvas_get_world_camera;
Call g_object_get_name;
Call g_camera_get_rect;
Call g_resources_find_all;
Call g_camera_get_orthographic;
Call g_camera_get_orthographic_size;
Call g_component_get_transform;
Call g_rect_transform_get_rect;
Call g_camera_get_aspect;
Call g_camera_reset_aspect;
Call g_texture_get_width;
Call g_texture_get_height;
Call g_transform_get_child_count;
Call g_transform_get_child;
Call g_screen_get_safe_area;
Call g_screen_get_current_resolution;
Call g_display_get_main;
Call g_display_get_rendering_width;
Call g_display_get_rendering_height;
Call g_display_get_system_width;
Call g_camera_get_main;
Call g_camera_get_pixel_width;
Call g_canvas_get_scale_factor;
Call g_canvas_set_scale_factor;
Call g_canvas_is_root;
Call g_camera_get_clear_flags;
Call g_camera_set_clear_flags;
Call g_camera_get_depth;
Call g_camera_get_culling_mask;
Call g_behaviour_get_enabled;
Call g_component_get_component;
Call g_urp_get_render_type;
Il2CppClass* g_urp_camera_data_class = nullptr;

void* display_main() {
    if (!g_display_get_main)
        return nullptr;
    return reinterpret_cast<Il2CppObject* (*)(const MethodInfo*)>(g_display_get_main.ptr)(
        g_display_get_main.info);
}

void* main_camera() {
    if (!g_camera_get_main)
        return nullptr;
    return reinterpret_cast<Il2CppObject* (*)(const MethodInfo*)>(g_camera_get_main.ptr)(
        g_camera_get_main.info);
}

int call_int(const Call& call, void* instance) {
    if (!call || !instance)
        return 0;
    return reinterpret_cast<int (*)(void*, const MethodInfo*)>(call.ptr)(instance, call.info);
}
Il2CppClass* g_canvas_class = nullptr;
Il2CppClass* g_camera_class = nullptr;

std::vector<void*> find_all(Il2CppClass* klass) {
    std::vector<void*> result;
    if (!g_resources_find_all || !klass)
        return result;

    Il2CppObject* type_object = il2cpp::api().type_get_object(il2cpp::api().class_get_type(klass));
    if (!type_object)
        return result;

    Il2CppObject* array = reinterpret_cast<Il2CppObject* (*)(void*, const MethodInfo*)>(
        g_resources_find_all.ptr)(type_object, g_resources_find_all.info);
    if (!array)
        return result;

    // Il2CppArray on x64: 16 byte object header, bounds pointer, length, then
    // the elements.
    const size_t count = il2cpp::api().array_length(array);
    void** items = reinterpret_cast<void**>(static_cast<char*>(array) + 32);
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (items[i])
            result.push_back(items[i]);
    }
    return result;
}

// This build only ships SetResolution(int, int, bool); the FullScreenMode
// overload was stripped because the game never calls it. When that is what we
// bound, the mode has to be translated rather than passed straight through.
bool g_set_resolution_takes_bool = false;

// Screen::get_width is hooked to report a 16:9 width to the game, so our own
// code has to remember what the real one is.
int g_real_screen_width = 0;

} // namespace

const Bindings& bindings() { return g_bindings; }

int screen_width() {
    if (!g_screen_width)
        return 0;
    return reinterpret_cast<int (*)(const MethodInfo*)>(g_screen_width.ptr)(g_screen_width.info);
}

int screen_height() {
    if (!g_screen_height)
        return 0;
    return reinterpret_cast<int (*)(const MethodInfo*)>(g_screen_height.ptr)(g_screen_height.info);
}

int real_screen_width() { return g_real_screen_width > 0 ? g_real_screen_width : screen_width(); }

void set_real_screen_width(int width) { g_real_screen_width = width; }

Rect screen_safe_area() {
    Rect rect{0.0f, 0.0f, 0.0f, 0.0f};
    if (!g_screen_get_safe_area)
        return rect;
    reinterpret_cast<void (*)(Rect*, const MethodInfo*)>(g_screen_get_safe_area.ptr)(
        &rect, g_screen_get_safe_area.info);
    return rect;
}

int display_rendering_width() { return call_int(g_display_get_rendering_width, display_main()); }

int display_rendering_height() { return call_int(g_display_get_rendering_height, display_main()); }

int main_camera_pixel_width() { return call_int(g_camera_get_pixel_width, main_camera()); }

float main_camera_aspect() { return camera_aspect(main_camera()); }

Resolution screen_current_resolution() {
    Resolution resolution{0, 0, 0, 0};
    if (!g_screen_get_current_resolution)
        return resolution;
    reinterpret_cast<void (*)(Resolution*, const MethodInfo*)>(
        g_screen_get_current_resolution.ptr)(&resolution, g_screen_get_current_resolution.info);
    return resolution;
}

float screen_aspect() {
    const int width = real_screen_width();
    const int height = screen_height();
    if (width <= 0 || height <= 0)
        return 0.0f;
    return static_cast<float>(width) / static_cast<float>(height);
}

void set_resolution(int width, int height, int fullscreen_mode) {
    if (!g_screen_set_resolution)
        return;

    int argument = fullscreen_mode;
    if (g_set_resolution_takes_bool) {
        // FullScreenMode 0 (exclusive) and 1 (borderless) are both fullscreen;
        // 2 (maximised) and 3 (windowed) are not. Unity then picks the exact
        // fullscreen style itself.
        argument = (fullscreen_mode == 0 || fullscreen_mode == 1) ? 1 : 0;
    }

    reinterpret_cast<void (*)(int, int, int, const MethodInfo*)>(g_screen_set_resolution.ptr)(
        width, height, argument, g_screen_set_resolution.info);
}

bool camera_has_target_texture(void* camera) {
    if (!g_camera_get_target_texture || !camera)
        return false;
    return reinterpret_cast<Il2CppObject* (*)(void*, const MethodInfo*)>(
               g_camera_get_target_texture.ptr)(camera, g_camera_get_target_texture.info) != nullptr;
}

int canvas_render_mode(void* canvas) {
    if (!g_canvas_get_render_mode || !canvas)
        return -1;
    return reinterpret_cast<int (*)(void*, const MethodInfo*)>(g_canvas_get_render_mode.ptr)(
        canvas, g_canvas_get_render_mode.info);
}

void* canvas_world_camera(void* canvas) {
    if (!g_canvas_get_world_camera || !canvas)
        return nullptr;
    return reinterpret_cast<Il2CppObject* (*)(void*, const MethodInfo*)>(
        g_canvas_get_world_camera.ptr)(canvas, g_canvas_get_world_camera.info);
}

bool canvas_has_world_camera(void* canvas) {
    return canvas_world_camera(canvas) != nullptr;
}

std::string type_name(void* unity_object) {
    if (!unity_object)
        return {};
    Il2CppClass* klass = il2cpp::api().object_get_class(unity_object);
    const char* name = klass ? il2cpp::api().class_get_name(klass) : nullptr;
    return name ? name : "";
}

// Rect is 16 bytes, so it comes back through a hidden return buffer that the
// x64 ABI places before the instance pointer.
Rect camera_rect(void* camera) {
    Rect rect{0.0f, 0.0f, 0.0f, 0.0f};
    if (!g_camera_get_rect || !camera)
        return rect;
    reinterpret_cast<void (*)(Rect*, void*, const MethodInfo*)>(g_camera_get_rect.ptr)(
        &rect, camera, g_camera_get_rect.info);
    return rect;
}

bool camera_is_orthographic(void* camera) {
    if (!g_camera_get_orthographic || !camera)
        return false;
    return reinterpret_cast<bool (*)(void*, const MethodInfo*)>(g_camera_get_orthographic.ptr)(
        camera, g_camera_get_orthographic.info);
}

int camera_render_type(void* camera) {
    if (!g_component_get_component || !g_urp_get_render_type || !g_urp_camera_data_class || !camera)
        return -1;

    Il2CppObject* type =
        il2cpp::api().type_get_object(il2cpp::api().class_get_type(g_urp_camera_data_class));
    if (!type)
        return -1;

    void* data = reinterpret_cast<Il2CppObject* (*)(void*, void*, const MethodInfo*)>(
        g_component_get_component.ptr)(camera, type, g_component_get_component.info);
    if (!data)
        return -1;

    return reinterpret_cast<int (*)(void*, const MethodInfo*)>(g_urp_get_render_type.ptr)(
        data, g_urp_get_render_type.info);
}

int camera_clear_flags(void* camera) { return call_int(g_camera_get_clear_flags, camera); }

void set_camera_clear_flags(void* camera, int flags) {
    if (!g_camera_set_clear_flags || !camera)
        return;
    reinterpret_cast<void (*)(void*, int, const MethodInfo*)>(g_camera_set_clear_flags.ptr)(
        camera, flags, g_camera_set_clear_flags.info);
}

int camera_culling_mask(void* camera) { return call_int(g_camera_get_culling_mask, camera); }

float camera_depth(void* camera) {
    if (!g_camera_get_depth || !camera)
        return 0.0f;
    return reinterpret_cast<float (*)(void*, const MethodInfo*)>(g_camera_get_depth.ptr)(
        camera, g_camera_get_depth.info);
}

bool behaviour_enabled(void* behaviour) {
    if (!g_behaviour_get_enabled || !behaviour)
        return false;
    return reinterpret_cast<bool (*)(void*, const MethodInfo*)>(g_behaviour_get_enabled.ptr)(
        behaviour, g_behaviour_get_enabled.info);
}

float camera_orthographic_size(void* camera) {
    if (!g_camera_get_orthographic_size || !camera)
        return 0.0f;
    return reinterpret_cast<float (*)(void*, const MethodInfo*)>(
        g_camera_get_orthographic_size.ptr)(camera, g_camera_get_orthographic_size.info);
}

float camera_aspect(void* camera) {
    if (!g_camera_get_aspect || !camera)
        return 0.0f;
    return reinterpret_cast<float (*)(void*, const MethodInfo*)>(g_camera_get_aspect.ptr)(
        camera, g_camera_get_aspect.info);
}

float camera_viewport_aspect(void* camera) {
    const float screen = screen_aspect();
    const Rect rect = camera_rect(camera);
    if (screen <= 0.0f || rect.width <= 0.0f || rect.height <= 0.0f)
        return screen;
    return screen * (rect.width / rect.height);
}

void camera_reset_aspect(void* camera) {
    if (!g_camera_reset_aspect || !camera)
        return;
    reinterpret_cast<void (*)(void*, const MethodInfo*)>(g_camera_reset_aspect.ptr)(
        camera, g_camera_reset_aspect.info);
}

void camera_target_texture_size(void* camera, int& width, int& height) {
    width = 0;
    height = 0;
    if (!g_camera_get_target_texture || !g_texture_get_width || !g_texture_get_height || !camera)
        return;

    void* texture = reinterpret_cast<Il2CppObject* (*)(void*, const MethodInfo*)>(
        g_camera_get_target_texture.ptr)(camera, g_camera_get_target_texture.info);
    if (!texture)
        return;

    width = reinterpret_cast<int (*)(void*, const MethodInfo*)>(g_texture_get_width.ptr)(
        texture, g_texture_get_width.info);
    height = reinterpret_cast<int (*)(void*, const MethodInfo*)>(g_texture_get_height.ptr)(
        texture, g_texture_get_height.info);
}

void* component_transform(void* component) {
    if (!g_component_get_transform || !component)
        return nullptr;
    return reinterpret_cast<Il2CppObject* (*)(void*, const MethodInfo*)>(
        g_component_get_transform.ptr)(component, g_component_get_transform.info);
}

int transform_child_count(void* transform) {
    if (!g_transform_get_child_count || !transform)
        return 0;
    return reinterpret_cast<int (*)(void*, const MethodInfo*)>(g_transform_get_child_count.ptr)(
        transform, g_transform_get_child_count.info);
}

void* transform_child(void* transform, int index) {
    if (!g_transform_get_child || !transform)
        return nullptr;
    return reinterpret_cast<Il2CppObject* (*)(void*, int, const MethodInfo*)>(
        g_transform_get_child.ptr)(transform, index, g_transform_get_child.info);
}

Rect transform_rect(void* transform) {
    Rect rect{0.0f, 0.0f, 0.0f, 0.0f};
    if (!g_rect_transform_get_rect || !transform)
        return rect;
    reinterpret_cast<void (*)(Rect*, void*, const MethodInfo*)>(g_rect_transform_get_rect.ptr)(
        &rect, transform, g_rect_transform_get_rect.info);
    return rect;
}

bool canvas_is_root(void* canvas) {
    if (!g_canvas_is_root || !canvas)
        return false;
    return reinterpret_cast<bool (*)(void*, const MethodInfo*)>(g_canvas_is_root.ptr)(
        canvas, g_canvas_is_root.info);
}

float canvas_scale_factor(void* canvas) {
    if (!g_canvas_get_scale_factor || !canvas)
        return 0.0f;
    return reinterpret_cast<float (*)(void*, const MethodInfo*)>(g_canvas_get_scale_factor.ptr)(
        canvas, g_canvas_get_scale_factor.info);
}

void set_canvas_scale_factor(void* canvas, float value) {
    if (!g_canvas_set_scale_factor || !canvas)
        return;
    reinterpret_cast<void (*)(void*, float, const MethodInfo*)>(g_canvas_set_scale_factor.ptr)(
        canvas, value, g_canvas_set_scale_factor.info);
}

Rect canvas_rect(void* canvas) {
    Rect rect{0.0f, 0.0f, 0.0f, 0.0f};
    if (!g_component_get_transform || !g_rect_transform_get_rect || !canvas)
        return rect;

    // A canvas GameObject carries a RectTransform, and Component.transform
    // hands back that very instance.
    void* transform = reinterpret_cast<Il2CppObject* (*)(void*, const MethodInfo*)>(
        g_component_get_transform.ptr)(canvas, g_component_get_transform.info);
    if (!transform)
        return rect;

    reinterpret_cast<void (*)(Rect*, void*, const MethodInfo*)>(g_rect_transform_get_rect.ptr)(
        &rect, transform, g_rect_transform_get_rect.info);
    return rect;
}

std::vector<void*> find_all_canvases() { return find_all(g_canvas_class); }
std::vector<void*> find_all_cameras() { return find_all(g_camera_class); }

std::string object_name(void* unity_object) {
    if (!g_object_get_name || !unity_object)
        return {};
    Il2CppObject* name = reinterpret_cast<Il2CppObject* (*)(void*, const MethodInfo*)>(
        g_object_get_name.ptr)(unity_object, g_object_get_name.info);
    return il2cpp::to_narrow(name);
}

bool resolve() {
    Il2CppClass* camera = il2cpp::find_class(kCoreModule, "UnityEngine", "Camera");
    Il2CppClass* screen = il2cpp::find_class(kCoreModule, "UnityEngine", "Screen");
    Il2CppClass* time = il2cpp::find_class(kCoreModule, "UnityEngine", "Time");
    if (!camera || !screen || !time)
        return false;

    g_bindings.camera_set_aspect = il2cpp::method_pointer(il2cpp::find_method(camera, "set_aspect", 1));
    const MethodInfo* set_rect = il2cpp::find_method(camera, "set_rect", 1);
    g_bindings.camera_set_rect = il2cpp::method_pointer(set_rect);
    g_bindings.camera_set_rect_info = set_rect;
    g_bindings.camera_set_field_of_view =
        il2cpp::method_pointer(il2cpp::find_method(camera, "set_fieldOfView", 1));
    g_camera_get_target_texture = make_call(il2cpp::find_method(camera, "get_targetTexture", 0));
    g_camera_get_rect = make_call(il2cpp::find_method(camera, "get_rect", 0));
    g_camera_get_orthographic = make_call(il2cpp::find_method(camera, "get_orthographic", 0));
    g_camera_get_aspect = make_call(il2cpp::find_method(camera, "get_aspect", 0));
    g_camera_get_clear_flags = make_call(il2cpp::find_method(camera, "get_clearFlags", 0));
    g_camera_set_clear_flags = make_call(il2cpp::find_method(camera, "set_clearFlags", 1));
    g_camera_get_depth = make_call(il2cpp::find_method(camera, "get_depth", 0));
    g_camera_get_culling_mask = make_call(il2cpp::find_method(camera, "get_cullingMask", 0));

    if (Il2CppClass* behaviour = il2cpp::find_class(kCoreModule, "UnityEngine", "Behaviour"))
        g_behaviour_get_enabled = make_call(il2cpp::find_method(behaviour, "get_enabled", 0));

    if (Il2CppClass* component = il2cpp::find_class(kCoreModule, "UnityEngine", "Component")) {
        g_component_get_component =
            make_call(il2cpp::find_overload(component, "GetComponent", 1, 0, "System.Type"));
    }

    g_urp_camera_data_class = il2cpp::find_class(kUrpModule, "UnityEngine.Rendering.Universal",
                                                 "UniversalAdditionalCameraData");
    if (g_urp_camera_data_class) {
        g_urp_get_render_type =
            make_call(il2cpp::find_method(g_urp_camera_data_class, "get_renderType", 0));
    }
    g_camera_reset_aspect = make_call(il2cpp::find_method(camera, "ResetAspect", 0));
    g_camera_get_orthographic_size =
        make_call(il2cpp::find_method(camera, "get_orthographicSize", 0));
    g_camera_class = camera;

    if (Il2CppClass* resources = il2cpp::find_class(kCoreModule, "UnityEngine", "Resources"))
        g_resources_find_all = make_call(il2cpp::find_method(resources, "FindObjectsOfTypeAll", 1));

    const MethodInfo* get_width = il2cpp::find_method(screen, "get_width", 0);
    g_screen_width = make_call(get_width);
    g_bindings.screen_get_width = il2cpp::method_pointer(get_width);
    g_bindings.screen_get_width_info = get_width;

    g_screen_get_safe_area = make_call(il2cpp::find_method(screen, "get_safeArea", 0));
    g_screen_get_current_resolution =
        make_call(il2cpp::find_method(screen, "get_currentResolution", 0));
    if (Il2CppClass* display = il2cpp::find_class(kCoreModule, "UnityEngine", "Display")) {
        g_display_get_main = make_call(il2cpp::find_method(display, "get_main", 0));
        g_display_get_rendering_width =
            make_call(il2cpp::find_method(display, "get_renderingWidth", 0));
        g_display_get_rendering_height =
            make_call(il2cpp::find_method(display, "get_renderingHeight", 0));
        g_display_get_system_width = make_call(il2cpp::find_method(display, "get_systemWidth", 0));
        g_bindings.display_get_rendering_width = g_display_get_rendering_width.ptr;
        g_bindings.display_get_system_width = g_display_get_system_width.ptr;
    }

    g_camera_get_main = make_call(il2cpp::find_method(camera, "get_main", 0));
    g_camera_get_pixel_width = make_call(il2cpp::find_method(camera, "get_pixelWidth", 0));

    g_bindings.screen_get_safe_area = g_screen_get_safe_area.ptr;
    g_bindings.screen_get_current_resolution = g_screen_get_current_resolution.ptr;
    g_screen_height = make_call(il2cpp::find_method(screen, "get_height", 0));
    // Screen::SetResolution has two three-argument forms (bool and
    // FullScreenMode); pick the FullScreenMode one explicitly.
    g_screen_set_resolution = make_call(
        il2cpp::find_overload(screen, "SetResolution", 3, 2, "FullScreenMode"));

    g_set_resolution_takes_bool =
        il2cpp::param_type_contains(g_screen_set_resolution.info, 2, "Boolean");

    // Time::get_deltaTime is our main-thread pump: it is called every frame by
    // ordinary game code, so hooking it gives us a safe place to re-assert the
    // resolution without spawning a thread that touches the Unity API.
    g_bindings.time_get_delta_time =
        il2cpp::method_pointer(il2cpp::find_method(time, "get_deltaTime", 0));

    if (Il2CppClass* texture = il2cpp::find_class(kCoreModule, "UnityEngine", "Texture")) {
        g_texture_get_width = make_call(il2cpp::find_method(texture, "get_width", 0));
        g_texture_get_height = make_call(il2cpp::find_method(texture, "get_height", 0));
    }

    if (Il2CppClass* component = il2cpp::find_class(kCoreModule, "UnityEngine", "Component"))
        g_component_get_transform = make_call(il2cpp::find_method(component, "get_transform", 0));

    if (Il2CppClass* rect_transform =
            il2cpp::find_class(kCoreModule, "UnityEngine", "RectTransform"))
        g_rect_transform_get_rect = make_call(il2cpp::find_method(rect_transform, "get_rect", 0));

    if (Il2CppClass* transform = il2cpp::find_class(kCoreModule, "UnityEngine", "Transform")) {
        g_transform_get_child_count =
            make_call(il2cpp::find_method(transform, "get_childCount", 0));
        g_transform_get_child = make_call(il2cpp::find_method(transform, "GetChild", 1));
    }

    if (Il2CppClass* object_class = il2cpp::find_class(kCoreModule, "UnityEngine", "Object"))
        g_object_get_name = make_call(il2cpp::find_method(object_class, "get_name", 0));

    if (Il2CppClass* canvas = il2cpp::find_class(kUiEngineModule, "UnityEngine", "Canvas")) {
        g_canvas_class = canvas;
        g_canvas_is_root = make_call(il2cpp::find_method(canvas, "get_isRootCanvas", 0));
        g_canvas_get_scale_factor = make_call(il2cpp::find_method(canvas, "get_scaleFactor", 0));
        g_canvas_set_scale_factor = make_call(il2cpp::find_method(canvas, "set_scaleFactor", 1));
        g_canvas_get_render_mode = make_call(il2cpp::find_method(canvas, "get_renderMode", 0));
        g_canvas_get_world_camera = make_call(il2cpp::find_method(canvas, "get_worldCamera", 0));
    }

    // uGUI is optional -- if the game scales its HUD some other way we still
    // apply every camera fix.
    if (Il2CppClass* scaler = il2cpp::find_class(kUiModule, "UnityEngine.UI", "CanvasScaler")) {
        g_bindings.canvas_scaler_handle =
            il2cpp::method_pointer(il2cpp::find_method(scaler, "Handle", 0));
        g_bindings.scaler_ui_scale_mode = il2cpp::field_offset(scaler, "m_UiScaleMode");
        g_bindings.scaler_screen_match_mode = il2cpp::field_offset(scaler, "m_ScreenMatchMode");
        g_bindings.scaler_match_width_or_height =
            il2cpp::field_offset(scaler, "m_MatchWidthOrHeight");
        g_bindings.scaler_reference_resolution =
            il2cpp::field_offset(scaler, "m_ReferenceResolution");
        g_bindings.scaler_canvas = il2cpp::field_offset(scaler, "m_Canvas");
        g_bindings.canvas_scaler_available = g_bindings.canvas_scaler_handle != nullptr &&
                                             g_bindings.scaler_ui_scale_mode != 0 &&
                                             g_bindings.scaler_match_width_or_height != 0;
    }

    LOG_INFO("SetResolution takes a bool: {}", g_set_resolution_takes_bool);
    LOG_INFO("Resolved bindings: set_aspect={} set_rect={} set_fieldOfView={} deltaTime={} canvasScaler={}",
             g_bindings.camera_set_aspect != nullptr, g_bindings.camera_set_rect != nullptr,
             g_bindings.camera_set_field_of_view != nullptr,
             g_bindings.time_get_delta_time != nullptr, g_bindings.canvas_scaler_available);

    return g_bindings.camera_set_aspect != nullptr;
}

} // namespace unity
