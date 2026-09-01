#pragma once

namespace fixes {
// Installs the inline hooks. Call once, after unity::resolve() succeeds.
bool install();

// The fraction of the screen width the HUD is allowed to occupy: 1.0 when the
// screen is no wider than LockAspect, and LockAspect/screen when it is. Every
// correction derives its geometry from this one number.
float lock_band();
} // namespace fixes
