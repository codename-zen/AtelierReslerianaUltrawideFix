#pragma once

namespace elements {

// Logs the RectTransform tree under a canvas: name, size, anchors and position.
// Used to identify an element by eye against a screenshot before nudging it.
void dump(void* canvas);

// Applies the NudgeElements list, shifting matching elements horizontally.
//
// This exists for layout the game itself gets wrong at a wide aspect. Its
// description panel is anchored with a fixed width, so a wider canvas carries
// it away from the label column beside it -- which happens with the mod doing
// nothing at all, verified with every adjustment disabled. A general rule would
// do more harm than a targeted shift, so the shift is named and opt-in.
void apply_nudges(void* canvas);

} // namespace elements
