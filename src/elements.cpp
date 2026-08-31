#include "elements.hpp"

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.hpp"
#include "log.hpp"
#include "unity_bindings.hpp"

namespace elements {
namespace {

constexpr int kMaxDepth = 12;
// The dump is a one-shot on a key press, so it can afford to walk the whole
// menu; the nudge pass runs every 250 ms and stays cheap.
constexpr int kMaxDumpNodes = 6000;
// The Inventory alone puts hundreds of item icons between the canvas root and
// the panel worth moving, and 600 ran out long before reaching it. The walk
// stops as soon as every entry has been placed, so the full budget is only
// spent when something is genuinely missing.
constexpr int kMaxNudgeNodes = 5000;
// Only containers are worth printing. Every icon and glyph in the tree would
// bury the panel being looked for.
constexpr float kMinDumpWidth = 1200.0f;
constexpr size_t kMaxTracked = 4096;

struct Nudge {
    std::string name;
    float offset = 0.0f;
};

struct Applied {
    float original_x = 0.0f;
    float applied_x = 0.0f;
};

std::unordered_map<void*, Applied> g_applied;

// "Name:offset, Other:-120"
std::vector<Nudge> parse_nudges(const std::string& list) {
    std::vector<Nudge> result;
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find(',', start);
        if (end == std::string::npos)
            end = list.size();

        const std::string entry = list.substr(start, end - start);
        const size_t colon = entry.rfind(':');
        if (colon != std::string::npos) {
            const size_t first = entry.find_first_not_of(" \t");
            const size_t last = entry.find_last_not_of(" \t", colon > 0 ? colon - 1 : 0);
            if (first != std::string::npos && last != std::string::npos && first <= last) {
                Nudge nudge;
                nudge.name = entry.substr(first, last - first + 1);
                try {
                    nudge.offset = std::stof(entry.substr(colon + 1));
                    if (!nudge.name.empty())
                        result.push_back(nudge);
                } catch (const std::exception&) {
                    LOG_WARN("Could not read a nudge offset from '{}'", entry);
                }
            }
        }
        start = end + 1;
    }
    return result;
}

// GetChild hands back a Transform, and not every one of them is a
// RectTransform: particle effects and plain object roots sit in the same tree.
// Calling RectTransform members on those returns garbage -- one dumped as a
// width of -1.48e30 -- so they are skipped rather than measured.
bool is_rect_transform(void* transform) {
    const std::string type = unity::type_name(transform);
    return type == "RectTransform";
}

void walk(void* transform, int depth, int& budget, bool logging,
          const std::vector<Nudge>& nudges, size_t& matched) {
    if (!transform || depth > kMaxDepth || budget <= 0)
        return;

    const int children = unity::transform_child_count(transform);
    for (int i = 0; i < children && budget > 0; ++i) {
        void* child = unity::transform_child(transform, i);
        if (!child)
            continue;
        --budget;

        if (!is_rect_transform(child)) {
            walk(child, depth + 1, budget, logging, nudges, matched);
            continue;
        }

        const std::string name = unity::object_name(child);
        const unity::Rect rect = unity::transform_rect(child);

        if (logging && rect.width >= kMinDumpWidth) {
            const unity::Vector2 position = unity::anchored_position(child);
            const unity::Vector2 minimum = unity::anchor_min(child);
            const unity::Vector2 maximum = unity::anchor_max(child);
            LOG_INFO("{:>{}}'{}' {:.0f}x{:.0f} anchors {:.3f}..{:.3f} pos {:.0f},{:.0f}", "",
                     depth * 2 + 2, name, rect.width, rect.height, minimum.x, maximum.x, position.x,
                     position.y);
        }

        for (const Nudge& nudge : nudges) {
            if (name.find(nudge.name) == std::string::npos)
                continue;

            unity::Vector2 position = unity::anchored_position(child);
            if (!std::isfinite(position.x))
                break;

            const auto existing = g_applied.find(child);
            if (existing != g_applied.end() &&
                std::fabs(position.x - existing->second.applied_x) < 0.5f) {
                ++matched;
                break; // Already carrying the shift.
            }

            Applied applied;
            applied.original_x = position.x;
            applied.applied_x = position.x + nudge.offset;
            position.x = applied.applied_x;
            unity::set_anchored_position(child, position);
            g_applied[child] = applied;

            static int logged = 0;
            if (logged < 12) {
                ++logged;
                LOG_INFO("Nudged '{}' x {:.0f} -> {:.0f}", name, applied.original_x,
                         applied.applied_x);
            }
            ++matched;
            break;
        }

        // Every entry accounted for, so the rest of the tree cannot matter.
        // The panel sits well before the end of the Inventory's icon list, and
        // walking the remainder was most of the cost.
        if (!nudges.empty() && matched >= nudges.size()) {
            budget = 0;
            return;
        }

        walk(child, depth + 1, budget, logging, nudges, matched);
    }
}

} // namespace

void dump(void* canvas) {
    if (!canvas)
        return;

    LOG_INFO("Hierarchy of '{}':", unity::object_name(canvas));
    int budget = kMaxDumpNodes;
    size_t matched = 0;
    walk(unity::component_transform(canvas), 0, budget, true, {}, matched);
}

void apply_nudges(void* canvas) {
    if (!canvas || g_config.nudge_elements.empty())
        return;

    const std::vector<Nudge> nudges = parse_nudges(g_config.nudge_elements);
    if (nudges.empty())
        return;

    if (g_applied.size() > kMaxTracked)
        g_applied.clear();

    int budget = kMaxNudgeNodes;
    size_t matched = 0;
    walk(unity::component_transform(canvas), 0, budget, false, nudges, matched);

    if (budget <= 0 && matched < nudges.size()) {
        static int warned = 0;
        if (warned < 3) {
            ++warned;
            LOG_WARN("Nudge walk of '{}' ran out of budget; an element deeper than {} nodes will "
                     "not be found",
                     unity::object_name(canvas), kMaxNudgeNodes);
        }
    }
}

} // namespace elements
