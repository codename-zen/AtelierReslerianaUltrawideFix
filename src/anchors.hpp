#pragma once

namespace anchors {

// Pushes a canvas's HUD into a centred band by remapping the horizontal anchors
// of its direct children, where `band` is the fraction of the canvas width the
// HUD should occupy.
//
// This is the alternative to narrowing the UI camera. Narrowing is exact, but it
// confines everything that camera draws, and in a full-screen menu that includes
// the backdrop, which is why the sides went black. Leaving the camera full width
// keeps the backdrop covering the screen, and the HUD is moved instead.
//
// Passing band = 1.0 is the identity mapping, which writes the stored originals
// back and so undoes the effect. There is deliberately no separate restore path:
// only objects seen in the current pass are ever written to, which keeps stale
// pointers out of it.
void remap(void* canvas, float band);

} // namespace anchors
