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
void apply_nudges(void* canvas, float band);

// Narrows one named container, wherever it was found. Safe to call repeatedly:
// a container already carrying the correction is left alone.
//
// Exposed because a periodic pass is not enough on its own. The recipe tree
// restores its container to the full canvas width every time it opens and
// measures it moments later, so the correction has to be re-applied the frame
// it is undone.
void correct_transform(void* rect_transform, const char* name, float band);

// Whether ConstrainElements names this object, which is how the hook recognises
// the screen it is looking for while walking up from a component.
bool wants_correction(const char* name);

} // namespace elements
