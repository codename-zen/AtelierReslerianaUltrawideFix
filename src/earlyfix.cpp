#include "earlyfix.hpp"

#include <array>
#include <cmath>
#include <string>

#include <safetyhook.hpp>

#include "config.hpp"
#include "elements.hpp"
#include "fixes.hpp"
#include "il2cpp_api.hpp"
#include "log.hpp"
#include "unity_bindings.hpp"

namespace earlyfix {
namespace {

SafetyHookInline g_scroll_rect_on_enable{};

// From the tree's scroll view up to the container the INI names is four steps:
// ScrollView, CommonList, Null_tree, Null_base, 11_mix_tree(Clone). Eight leaves
// room for a screen laid out slightly differently without letting an unrelated
// scroll view walk all the way to the top of the canvas.
constexpr int kMaxAncestors = 8;

// One entry per named container the game has shown at least once. A handful is
// plenty: these are whole screens, not individual elements.
struct Watched {
    void* transform = nullptr;
    std::string name;
};
std::array<Watched, 8> g_watched;

void watch(void* transform, const std::string& name) {
    for (const Watched& entry : g_watched) {
        if (entry.transform == transform)
            return;
    }
    for (Watched& entry : g_watched) {
        if (!entry.transform) {
            entry.transform = transform;
            entry.name = name;
            return;
        }
    }
}

// ScrollRect::OnEnable is ordinary managed code in the uGUI package, so unlike
// an `extern` property setter there is nothing to inline it into: the runtime
// invokes it through the very pointer this hooks. It is what identifies the
// screen, and it fires on every opening.
void on_enable_hook(void* self, const MethodInfo* method) {
    g_scroll_rect_on_enable.unsafe_call<void>(self, method);

    void* transform = unity::component_transform(self);
    for (int level = 0; transform && level < kMaxAncestors; ++level) {
        const std::string name = unity::object_name(transform);
        if (elements::wants_correction(name.c_str())) {
            static int logged = 0;
            if (logged < 8) {
                ++logged;
                LOG_INFO("Watching '{}'@{}; its width goes back the frame the game takes it away",
                         name, transform);
            }
            watch(transform, name);
            elements::correct_transform(transform, name.c_str(), fixes::lock_band());
            return;
        }
        transform = unity::transform_parent(transform);
    }
}

} // namespace

void hold() {
    const float band = fixes::lock_band();
    if (band >= 0.999f)
        return;

    const float pad = (1.0f - band) * 0.5f;
    for (Watched& entry : g_watched) {
        if (!entry.transform)
            continue;

        // The cheap check first, because this runs every frame and almost every
        // frame there is nothing to do.
        const unity::Vector2 minimum = unity::anchor_min(entry.transform);
        if (!std::isfinite(minimum.x) || std::fabs(minimum.x - pad) < 0.001f)
            continue;

        // Only now is it worth confirming the pointer still refers to what it
        // did. An object can be collected and its address reused, and this is
        // the only path that writes.
        if (unity::object_name(entry.transform) != entry.name) {
            entry.transform = nullptr;
            continue;
        }

        elements::correct_transform(entry.transform, entry.name.c_str(), band);
    }
}

bool install() {
    if (g_config.constrain_elements.empty())
        return false;

    Il2CppClass* scroll_rect =
        il2cpp::find_class("UnityEngine.UI.dll", "UnityEngine.UI", "ScrollRect");
    if (!scroll_rect) {
        LOG_ERROR("UnityEngine.UI.ScrollRect unavailable; corrections fall back to the periodic "
                  "pass");
        return false;
    }

    void* target = il2cpp::method_pointer(il2cpp::find_method(scroll_rect, "OnEnable", 0));
    if (!target) {
        LOG_WARN("ScrollRect::OnEnable not found; corrections fall back to the periodic pass");
        return false;
    }

    g_scroll_rect_on_enable =
        safetyhook::create_inline(target, reinterpret_cast<void*>(&on_enable_hook));
    if (!g_scroll_rect_on_enable) {
        LOG_ERROR("Failed to hook ScrollRect::OnEnable");
        return false;
    }

    LOG_INFO("Hooked UnityEngine.UI.ScrollRect::OnEnable at {}", target);
    return true;
}

} // namespace earlyfix
