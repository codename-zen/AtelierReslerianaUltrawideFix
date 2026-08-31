#include "anchors.hpp"

#include <cmath>
#include <string>
#include <unordered_map>

#include "config.hpp"
#include "log.hpp"
#include "unity_bindings.hpp"

namespace anchors {
namespace {

struct State {
    float original_min = 0.0f;
    float original_max = 0.0f;
    float applied_min = 0.0f;
    float applied_max = 0.0f;
};

// Keyed by RectTransform. Entries are only ever written through after the object
// has been re-enumerated in the current pass, so a dead pointer is never
// dereferenced.
std::unordered_map<void*, State> g_states;

constexpr size_t kMaxTracked = 8192;
constexpr float kEpsilon = 0.0005f;
// The walk is bounded: a runaway hierarchy must not stall a frame.
constexpr int kMaxDepth = 8;
constexpr int kMaxNodes = 400;

bool close_enough(float a, float b) { return std::fabs(a - b) < kEpsilon; }

// A wrong ABI read would come back as garbage rather than a normalised anchor,
// and writing that garbage would scatter the UI. Anchors outside this range are
// left alone.
bool plausible_anchor(float value) {
    return std::isfinite(value) && value > -10.0f && value < 10.0f;
}

bool name_matches_list(const std::string& name, const std::string& list) {
    if (name.empty())
        return false;

    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(',', start);
        if (end == std::string::npos)
            end = list.size();

        const size_t first = list.find_first_not_of(" \t", start);
        const size_t last = list.find_last_not_of(" \t", end > start ? end - 1 : start);
        if (first != std::string::npos && last != std::string::npos && first <= last) {
            const std::string entry = list.substr(first, last - first + 1);
            // Substring, because the game suffixes clones with "(Clone)".
            if (!entry.empty() && name.find(entry) != std::string::npos)
                return true;
        }
        start = end + 1;
    }
    return false;
}

bool is_background(const std::string& name) {
    return name_matches_list(name, g_config.background_canvas_names);
}

// Reporting stays silent until the lock is actually in force. The first seconds
// run at the game's own 16:9 resolution, where the mapping is the identity, and
// those lines were consuming the whole budget before the interesting state was
// ever reached.
void report_child(void* child, float min_x, float max_x, const char* outcome, float band) {
    static int logged = 0;
    if (band > 0.999f || logged >= 50)
        return;
    ++logged;
    LOG_INFO("    '{}' x {:.3f}..{:.3f} [{}]", unity::object_name(child), min_x, max_x, outcome);
}

// Whether this node is what spreads the UI outwards. A node pinned exactly at
// the centre carries no horizontal spread of its own, so the layout that matters
// is further down and the walk continues through it. Anything stretched or
// anchored off-centre is what reaches for the screen edge, so that is the node
// to move.
bool spreads_horizontally(float min_x, float max_x) {
    const bool stretched = std::fabs(max_x - min_x) > 0.001f;
    const bool off_centre = std::fabs(min_x - 0.5f) > 0.001f || std::fabs(max_x - 0.5f) > 0.001f;
    return stretched || off_centre;
}

// Walks down from a canvas and remaps the topmost spreading node in each branch,
// then stops descending that branch.
//
// Remapping only the direct children was too shallow: the menus sit several
// levels down behind a centred wrapper, so nothing that mattered was reached.
// Remapping every canvas was too deep: a nested canvas was moved on top of its
// already-moved parent, leaving the UI at 0.744 squared, which is what
// scrambled it. Stopping at the first spreading node per branch applies the
// mapping exactly once.
void walk(void* transform, float band, float pad, int depth, int& budget) {
    if (!transform || depth > kMaxDepth || budget <= 0)
        return;

    const int children = unity::transform_child_count(transform);
    for (int i = 0; i < children && budget > 0; ++i) {
        void* child = unity::transform_child(transform, i);
        if (!child)
            continue;
        --budget;

        // Not every Transform in the tree is a RectTransform; effects and
        // plain roots are not, and reading anchors off those returns garbage.
        if (unity::type_name(child) != "RectTransform") {
            walk(child, band, pad, depth + 1, budget);
            continue;
        }

        const std::string name = unity::object_name(child);
        if (is_background(name)) {
            report_child(child, 0.0f, 0.0f, "background", band);
            continue;
        }

        unity::Vector2 minimum = unity::anchor_min(child);
        unity::Vector2 maximum = unity::anchor_max(child);
        if (!plausible_anchor(minimum.x) || !plausible_anchor(maximum.x)) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                LOG_WARN("Anchors read back as {:.3f}..{:.3f}, which is not a normalised range; "
                         "leaving them alone",
                         minimum.x, maximum.x);
            }
            continue;
        }

        const auto existing = g_states.find(child);
        if (existing != g_states.end() && close_enough(minimum.x, existing->second.applied_min) &&
            close_enough(maximum.x, existing->second.applied_max)) {
            continue; // Ours already, and its branch with it.
        }

        if (!spreads_horizontally(minimum.x, maximum.x)) {
            report_child(child, minimum.x, maximum.x, "centred, descending", band);
            walk(child, band, pad, depth + 1, budget);
            continue;
        }

        State state;
        state.original_min = minimum.x;
        state.original_max = maximum.x;
        state.applied_min = pad + minimum.x * band;
        state.applied_max = pad + maximum.x * band;

        if (close_enough(state.applied_min, minimum.x) &&
            close_enough(state.applied_max, maximum.x)) {
            g_states[child] = state;
            report_child(child, minimum.x, maximum.x, "identity, left alone", band);
            continue;
        }

        minimum.x = state.applied_min;
        maximum.x = state.applied_max;
        unity::set_anchor_min(child, minimum);
        unity::set_anchor_max(child, maximum);
        g_states[child] = state;

        static int logged = 0;
        if (logged < 24) {
            ++logged;
            LOG_INFO("Anchored '{}' x {:.3f}..{:.3f} -> {:.3f}..{:.3f} (depth {})", name,
                     state.original_min, state.original_max, state.applied_min, state.applied_max,
                     depth);
        }
        // No recursion here on purpose: everything below moves with this node.
    }
}

} // namespace

void remap(void* canvas, float band) {
    if (!canvas || band <= 0.0f)
        return;

    // The backdrop and blur layers are exactly what the full-width camera is
    // for, so they keep their anchors.
    if (is_background(unity::object_name(canvas)))
        return;

    if (g_states.size() > kMaxTracked)
        g_states.clear();

    int budget = kMaxNodes;
    walk(unity::component_transform(canvas), band, (1.0f - band) * 0.5f, 0, budget);
}

} // namespace anchors
