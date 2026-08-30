#pragma once

namespace bootstrap {

// Arranges for the fix to be set up on the game's main thread, once the il2cpp
// runtime is fully initialised. Call once from the loader thread.
void install();

} // namespace bootstrap
