#pragma once

namespace earlyfix {

// Keeps a container narrow against a game that keeps widening it again.
//
// The recipe tree is the case this was built for. Its container is anchored
// 0..1, so it stretches to whatever the canvas is: 3840 units at 16:9, 5160 at
// 21:9. The game reads that width once, when it fills the tree in, and writes
// every node position from it. Reading 5160 and then being shown inside a 16:9
// frame is what puts the first node 660 units off the left edge -- exactly half
// the difference.
//
// What makes it stubborn is that the game restores those 0..1 anchors every time
// the screen opens, moments before it lays the tree out. The container is not
// rebuilt -- its address is the same all session -- so this is a write, not a
// new object, and it lands after the screen has already come alive.
//
// That write cannot be intercepted. RectTransform's anchor properties are
// `extern` declarations, and this IL2CPP build inlines the icall straight into
// its callers, so the managed setter whose method pointer we can hook is never
// executed by game code. Hooking it caught only our own writes coming back
// through it, which for one round made a broken thing look like a working one.
// The Instantiate funnel failed for the same reason: in this build, only
// ordinary managed methods are hookable.
//
// So the anchors are held rather than defended. ScrollRect::OnEnable identifies
// the screen -- real managed code in the uGUI package, invoked by the runtime
// through the pointer we hook, so nothing can inline it away -- and from then on
// every frame checks the container and puts the width back the moment the game
// takes it away. A frame is not zero, but it is a fortieth of the 250 ms pass
// that was losing this race.
bool install();

// Re-applies the correction to every watched container. Called once per frame
// from the main-thread pump, unthrottled: the whole point is the gap between
// the game's write and its measurement, and that gap is measured in frames.
void hold();

} // namespace earlyfix
