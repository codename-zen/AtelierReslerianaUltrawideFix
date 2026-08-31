# Implementation guide

How this fix is put together, why it is built this way, and what was learned the hard way.

## 1. Identifying the engine

Everything below came from the installed build rather than from community threads, and each check is worth repeating if the game updates.

```sh
G="C:/Program Files (x86)/Steam/steamapps/common/AtelierReslerianaRW"

# Koei Tecmo engine or Unity?
ls "$G"                                    # UnityPlayer.dll, GameAssembly.dll, UnityCrashHandler64.exe
cat "$G/AtelierReslerianaRW_Data/app.info" # KoeiTecmo / AtelierReslerianaRW

# Exact Unity version.
head -c 400 "$G/AtelierReslerianaRW_Data/globalgamemanagers" | tr -d '\000' \
  | grep -ao '[0-9]\{4\}\.[0-9]\+\.[0-9]\+[a-z][0-9]\+'    # 2022.3.56f1
```

Unity 2022.3.56f1, IL2CPP, URP, D3D11.
A Koei Tecmo engine title would ship `.g1t` textures and `.pak` archives and have no `*_Data` folder, so Lyall's `dinput8.dll` plus memory-pattern approach for Atelier Yumia does not transfer.

`%USERPROFILE%\AppData\LocalLow\KoeiTecmo\AtelierReslerianaRW\Player.log` is the other high-value source: it names the graphics device and prints managed stack traces, which is how the Beebyte obfuscation was confirmed.

## 2. The metadata problem, and why it dictates the architecture

The identifier string table in `global-metadata.dat` is encrypted.

```sh
M="$G/AtelierReslerianaRW_Data/il2cpp_data/Metadata/global-metadata.dat"
od -N 8 -t x1 "$M"                # af 1b b1 fa 1f 00 00 00 -> valid magic, metadata v31
for s in .ctor System Void get_; do grep -aoc -- "$s" "$M"; done   # all zero
```

A normal 40 MB metadata file contains hundreds of thousands of those substrings.
Zero occurrences means the header is intact, so the runtime can load it, but the strings are encrypted at rest.

That breaks the usual toolchain, because Cpp2IL parses that file from disk and both BepInEx 6 and MelonLoader use Cpp2IL to generate their interop assemblies.

Two facts make the native route work instead:

1. The runtime decrypts the names in memory, so by the time `il2cpp_init` returns, names are real.
2. `GameAssembly.dll` exports the full il2cpp C API (254 `il2cpp_*` symbols).

So the plugin resolves everything by name at runtime and never touches the metadata file.
It is also immune to the Beebyte renaming, because it only ever names Unity's own types, which Beebyte cannot rename.

## 3. When is it safe to call il2cpp

This is the part that is easy to get wrong, and getting it wrong kills the process outright.
It was gotten wrong twice in this project, in two different ways, so it is worth stating plainly.

The first version waited on its own thread until `il2cpp_domain_get()` returned non-null, called `il2cpp_thread_attach`, and started resolving names.
The game died at startup with **`Fatal error in GC: Collecting from unknown thread`**.

**A non-null domain does not mean the runtime is ready.**
The domain pointer appears part-way through `il2cpp_init`, while the garbage collector is still coming up, and the first allocating call from a thread the GC has never registered aborts the process.

The fix is to never call il2cpp from our own thread:

1. `DllMain` spawns a thread. No work happens under the loader lock.
2. That thread hooks `LoadLibraryA/W/ExA/ExW`, and also polls for `GameAssembly.dll`.
   Both paths exist because Steam's overlay hooks the same loader functions, and losing that race once left a whole session with no hooks and no clue in the log.
3. Either path installs a hook on the exported `il2cpp_init`.
4. The detour calls the original and only then runs `unity::resolve()` and `fixes::install()`, on the game's main thread with a fully initialised runtime.

The second failure was a "helpful" fallback that treated a live domain as readiness after a three second sleep.
Decrypting 40 MB of metadata takes longer than that, so it reintroduced the same crash through a different door.
The fallback now fires only after a full 60 second timeout with no setup at all, by which point `il2cpp_init` has certainly finished.

## 4. What the game's UI actually is

This took several rounds to pin down, and every intermediate theory was wrong in an instructive way.

- The live UI canvases are **Screen Space - Camera**, drawn by `UIDummyCamera`, `UIEffectDummyCamera` and `EventCamera`.
  Entries showing `renderMode=2` with `rect=0x0` in a dump are prefabs, not live instances.
- The game's main UI has **no stock `CanvasScaler`**.
  Hooking `CanvasScaler::Handle` caught only infrastructure canvases such as `FadeCanvas` and `LoadingUICanvas`.
- The game computes its own UI reference as **`(2160 x screen aspect)`**.
  At 21:9 that is 5160 wide, which is why a canvas came out `5160x2160` at full width and `5160x2902` once the viewport was narrowed.
- It does not read that aspect from `Screen.width`, `Screen.safeArea`, `Screen.currentResolution` or `Display.renderingWidth`.
  All four were spoofed to 16:9 and the canvas stayed inflated, so input spoofing was abandoned.
- The game never calls `Canvas::set_scaleFactor` for those canvases either.
  Hooking the setter produced zero hits while the canvases were plainly mis-scaled.

Two consequences follow, and together they are the fix:

**A screen-space canvas is always as wide as its camera's viewport.**
No `CanvasScaler` setting can pillarbox a UI, because anchors are normalised to the canvas rect.
Narrowing the UI camera's viewport is therefore not one option among several, it is the only one.

**The canvas size is `viewportPixels / scaleFactor`.**
A canvas inflated by exactly `screenAspect / 16:9` has a scale factor too small by that same factor, so writing the scale factor directly corrects it, with no need to know where the game got its aspect from.

## 5. The hooks

Every target is a managed method reached through `MethodInfo::methodPointer`, the first field of `MethodInfo` in every il2cpp version.
That is the only struct field this code relies on.
IL2CPP's native convention is `(instance, args..., const MethodInfo*)`, or `(args..., const MethodInfo*)` for statics.

| Managed method | What it does here |
| --- | --- |
| `Camera::set_aspect(float)` | Rewrites a forced aspect to the camera's own **viewport** aspect |
| `Camera::set_rect(Rect)` | Undoes centred letterboxing; `Rect` is 16 bytes so the x64 ABI passes it **by pointer** |
| `Camera::set_fieldOfView(float)` | Optional FOV offset and Vert- correction |
| `Time::get_deltaTime()` | Per-frame main-thread pump for everything below |
| `Screen`/`Display` getters | Aspect spoofing, off by default: it did not work and broke the options screen |

Two details worth keeping in mind:

**Aspect must follow the viewport, not the screen.**
Forcing screen aspect onto a camera narrowed to a 16:9 band gives it a projection wider than its viewport, which squashes everything it draws by 26 percent.
That was the "penyet" character bug.

**`SetResolution` has two three-argument overloads.**
This build only ships `SetResolution(int, int, bool)`; the IL2CPP linker stripped the `FullScreenMode` one.
`unity::set_resolution` checks the bound method's parameter type once and translates modes 0 and 1 to `true`, 2 and 3 to `false`.

## 6. The periodic pass

Runs from the `deltaTime` pump every 250 ms.

1. **Pin the UI cameras** to a centred band of `LockAspect`, and reset their aspect so the projection follows.
   Cameras named in `SceneCameraNames` are never pinned, because the 3D view must keep the full width.
   When the lock does not apply the fraction becomes 1.0, which releases cameras pinned earlier -- forgetting that left the UI squeezed twice over whenever the game flipped back to a 16:9 resolution of its own accord.
2. **Correct the canvas scale** for root canvases only.
   Nested canvases carry their own `RectTransform` -- the key guide is 2000x120 -- and snapping those to a design height inflated them wildly.
3. Only inflation that can be accounted for is undone: the height must match `design x (screenAspect / lockAspect)` within 1.5 percent, where design is one of 2160, 1440, 1080 or 720.
   Anything else is a size the game meant.
   This also makes the pass idempotent, since a corrected canvas sits at its design height and no longer matches the pattern.

## 7. Verification

Read the log in this order. Skipping the first step wasted a full round of analysis on a session where the plugin was not running at all.

1. `Hooked il2cpp_init at ...` followed by `Hooks installed.`
   Without both, nothing on screen is attributable to the mod.
2. `Applying 3440x1440 ...` appearing **once**, not repeatedly.
   Repeats mean the game is fighting the resolution back.
3. `Pinned camera 'UIDummyCamera' ...` and the other UI cameras.
4. `Canvas '...' 2902 units tall -> design 2160; scaleFactor 0.4961 -> 0.6667`.
5. `Canvas '...' is 3840x2160` for the main UI canvases.

A useful objective check on screenshots, since 3440x1440 pillarboxed to 16:9 gives a 2560 px band:

```
konten   : 440 .. 2999   lebar 2560  (0.7444 dari layar)
```

## 8. Known limitations

**In-game full-screen menus have black sides.**
This was investigated properly rather than assumed, and the conclusion is that it cannot be fixed from here.

URP camera types turned out to matter.
A **base** camera owns the final image area, so narrowing one confines everything behind the UI as well; the UI itself is drawn by an **overlay** camera, and narrowing only that still shrinks the canvas.
Releasing the base cameras fixed the title screen outright, which now fills the full ultrawide with its menu text still inside the 16:9 area.

It did not fix in-game menus, and neither did relaxing the clear on the full-screen camera that draws nothing (`cullingMask = 0`, `clearFlags = SolidColor`).
That change applied cleanly and repeatedly -- the game re-applies the clear every frame -- and the sides stayed black.

The reason is that in those menus the game renders no scene at all.
The blurred backdrop is a capture of the field, drawn as a UI element rather than rendered live, and the camera that draws it is the one that has to be narrowed.
With the base cameras already full width and still nothing appearing out there, there is nothing to reveal.
The title screen differs because its base camera draws a real 3D scene.

The last idea worth trying was that the backdrop might hang off a different camera instance from the HUD: three cameras share the name `UIDummyCamera`, and if the backdrop used one of the others it could stay full width while the HUD was constrained.
Logging the camera pointer per canvas settled it -- every canvas in a menu resolves to the same instance:

```
'CaptureCanvas'          camera='UIDummyCamera'@0x27431314ce0
'16_mainmenu_top(Clone)' camera='UIDummyCamera'@0x27431314ce0
'CharaRenderer'          camera='UIDummyCamera'@0x27431314ce0
```

The capture itself is a full 3440x1440 render texture, so the pixels for those sides do exist.
A viewport applies per camera, not per element, so there is no way to show the backdrop wide while keeping the HUD narrow.

The only remaining approach would be to leave the camera full width and re-anchor every HUD element into a centred band instead, excluding the backdrop.
That means walking each canvas's children, remapping their X anchors from `[0,1]` to `[0.128, 0.872]`, and storing the originals so the pass stays idempotent.
It is a large amount of fragile work that would reopen the layout problems already solved here, which is why it was not attempted.

Gameplay is unaffected: `Main Camera` is never pinned, so field and battle fill the full width.

**Aspect spoofing is off by default.**
It never fixed the canvas inflation, and it made the options screen report the wrong resolution and the game fight over it.

## 9. The anchor experiment, and why it failed

Narrowing the UI camera confines everything that camera draws, including the
menu backdrop, which is what leaves the sides of a full-screen menu black.
The alternative was to keep the camera full width and pull the HUD into a
centred band by remapping the horizontal anchors of its containers, mapping
`[0,1]` to `[0.128, 0.872]` at 3440x1440.

Half of it worked: the black sides disappeared, and the backdrop covered the
whole screen. The HUD never followed.

Three rules were tried, and the reason each failed is the useful part:

- **Direct children of root canvases only.** Too shallow. Two elements moved in
  the whole game, because the menus sit several levels below a wrapper anchored
  dead centre.
- **Every canvas.** Too deep. A nested canvas was moved on top of its
  already-moved parent, leaving the UI at 0.744 squared and visibly scrambled.
- **Walk down and remap the topmost spreading node per branch.** Correct in
  principle, and still only two elements moved.

That last result is what settled it.
The game does not stretch its menu containers to fit their parent, it assigns
them a size: a presenter canvas measures 5160x2160 whatever its parent does,
because 5160 is `2160 x screen aspect`.
Remapping an ancestor's anchors moves that container's origin and changes
nothing about its width, so the menu keeps spanning the screen while the
container around it shrinks -- which shows up as a panel sliding out from under
its own label.

Anchors are therefore the wrong lever for this game.
The lever that would work is the same one already used for the scale factor:
write the container's width directly, setting `sizeDelta.x` to
`2160 x 1.7778`. That was left undone deliberately, since fighting a size the
game rewrites every frame is a good deal riskier than the viewport route, which
already produces an exact 16:9 HUD.

The code is kept behind `Mode = anchors` because the approach is sound for a
title whose UI is anchor-driven; it is simply not this one.

## 10. Render textures shown in fixed slots

The Party screen draws its live character with a camera that renders into a
screen-sized texture, and the UI shows that texture in a slot of fixed size.
The whole texture is mapped onto the slot however wide it got, so a wider screen
squeezes everything drawn into it.

At 16:9 the texture is 2560x1440; at 3440x1440 it is 2.3889 wide against a slot
built for 1.7778, and the character came out 26 percent narrow -- `1.7778 /
2.3889 = 0.744`, the same factor that turned up in the canvas inflation.

The fix is compensation rather than correction. The texture stays screen-sized
and the camera renders at `LockAspect` instead, which stretches the image inside
the texture by 1.344; the slot then squeezes it by 0.744 and the two cancel.

The condition is deliberately narrow -- a camera with a target texture whose
aspect is wider than `LockAspect` -- so cameras drawing straight to screen are
untouched, and so is any texture already at 16:9 or narrower. In practice
exactly one camera matches. If some other render target ever looks stretched
horizontally, `CorrectRenderTextureAspect = false` is the first thing to try.

Worth separating from the earlier "penyet" bug, which looked identical on
screen. That one was a camera whose *own* aspect had been forced to the screen's
while its viewport was 16:9. This one is a camera whose aspect is right for its
texture, and wrong for where the texture ends up.

## 11. Per-element nudges

Some layout is wrong at a wide aspect before this fix touches anything.
The Inventory description panel is the clear case: `Image_win`, 3544x756, anchored dead centre, while the label column beside it is pinned left.
At 16:9 the canvas is 3840 units and the panel spans 148..3692, so the labels sit on it.
At 21:9 the canvas is 5160 and the same centred panel spans 819..4363, pulling 660 units clear of its own labels.

That 660 is half the canvas growth, `(5160 - 3840) / 2`, and it is the whole fix: `NudgeElements = Image_win:-660`.

It was verified as the game's own behaviour, not this fix's, before anything was written.
With `Mode = off` and `LockAspect = 0` the log recorded zero anchor changes, zero pinned cameras and zero scale corrections, and the panel still sat in exactly the same wrong place.

Three things the walk needed, all found the slow way:

- **Not every Transform is a RectTransform.**
  `GetChild` returns plain Transforms for particle effects, and reading anchors off one produced a width of -1.48e30 in a dump. They are stepped over now, and their children still visited.
- **Depth costs budget.**
  The panel sits behind several hundred item icons, so a 600 node walk never reached it while the 6000 node dump did. At a one second cadence the panel then visibly jumped into place after a menu opened, so the walk had to get cheaper rather than rarer.
- **Stopping early is the wrong economy.**
  Halting the walk once every entry had been placed looked like the obvious saving, and it broke the feature: the game keeps every menu instantiated at once, so the first `Image_win` found ended the walk and Equipment and Tools were never reached. Only Inventory was ever fixed.
  Skipping branches whose GameObject is inactive is the saving that actually works. Most of the tree is a menu nobody is looking at, so it costs far less than the early exit did, and what remains to walk is the menu on screen -- which is why all three panels are now found at a 250 ms cadence with budget to spare.

**A nudge offset is only valid for one `Mode`.**
The offset is derived from how far the canvas grew, so it depends on how wide the canvas is, and that is exactly what `Mode` decides.
`-660` is right for `Mode = off`, where the canvas reaches 5160 units at 3440x1440.
Under `Mode = viewport` the canvas is already 3840, the panel never drifts at all, and the same line pushes it out the other way.
The shipped defaults are therefore paired: `Mode = off` with the nudge set, and switching to `viewport` means clearing `NudgeElements`.

This was learned by clobbering it. Deploying the repository's INI over the one in the game folder replaced a working `Mode = off` with `viewport`, which brought the black bars back and misapplied the nudge on top -- looking far worse than either change alone, and not caused by the build it arrived with.

Matching is by substring, so clone suffixes do not matter -- and neither does specificity, which is the catch.
`Image_win` is a generic name, so every panel using it moves. That is right where the same displacement applies and wrong where a panel is meant to stay centred, so a new nudge is worth checking across a few screens.

## 12. The plugin loads in more than one process

`UnityCrashHandler64.exe` sits in the same folder and imports `VERSION.dll` too, so the loader picks this plugin up there as well.

That process has no `GameAssembly.dll` and never will, so the copy running in it span for the full sixty second timeout and then wrote `GameAssembly.dll never appeared; nothing was hooked` into the same log file the game was writing to. It read like a failure in the game's own copy, and survived one attempt to fix it in the bootstrap loop -- naturally, since `g_setup_done` in that process was genuinely false the whole time. Different process, different memory.

The check is now the first thing `initialise` does, before the log is even opened, so the wrong process never touches the file at all. Anything other than `AtelierReslerianaRW.exe` returns immediately.

Worth remembering when reading a log that contradicts itself: two processes can be writing to it.

## 13. Adding another hook

1. Resolve it in `unity::resolve()` with `il2cpp::method_pointer(il2cpp::find_method(klass, "Name", argc))`.
2. Write a detour matching IL2CPP's convention, remembering the trailing `const MethodInfo*`, and pass structs larger than 8 bytes by pointer.
3. Register it in `fixes::install()` via `create(...)`, which logs success and failure.

Field access needs no hook: `il2cpp::field_offset(klass, "m_FieldName")` plus a cast on the object pointer is enough.
