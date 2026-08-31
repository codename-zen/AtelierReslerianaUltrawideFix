# AtelierReslerianaFix

Ultrawide (21:9 / 32:9) fix for **Atelier Resleriana: The Red Alchemist & the White Guardian** (Steam, AppID 3259600).

Modelled on the structure of Lyall's `AtelierYumiaFix`: a single ASI plugin plus an INI, loaded by Ultimate ASI Loader.
The engine underneath is completely different from Atelier Yumia's, so the hooking strategy is different - see [docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md).

## What the game actually is

Verified against the installed build, not from community rumour:

| Property | Value | How it was verified |
| --- | --- | --- |
| Engine | **Unity 2022.3.56f1**, not the Koei Tecmo engine | `globalgamemanagers`, `UnityPlayer.dll`, `UnityCrashHandler64.exe` |
| Scripting backend | **IL2CPP** | `GameAssembly.dll` + `il2cpp_data/` |
| Render pipeline | **URP** with camera stacks | `Unity.RenderPipelines.Universal.Runtime.dll`, `RenderCameraStack` in the player log |
| Graphics API | Direct3D 11 | player log |
| UI | uGUI (`UnityEngine.UI`, TextMeshPro) | `ScriptingAssemblies.json` |
| Camera driver | Cinemachine | `Cinemachine.dll` |
| Middleware | GameCreator 2, UniTask, UniRx, Addressables | `ScriptingAssemblies.json` |
| Game code | Obfuscated with Beebyte (`KLLPPJFFKLN`-style names) | player log stack traces |
| `global-metadata.dat` | **String table is encrypted** | header parse: zero plaintext identifiers in 40 MB |
| Anti-tamper | None | no Denuvo/VMProtect/Themida markers, stock PE sections |

The publisher folder is named `KoeiTecmo`, but there is no `.g1t`, no `.pak`, and no Koei Tecmo engine code.
This build reuses the mobile game's Unity codebase, which is why the internal namespace is `Broom` and the Addressables bundles are encrypted.

## Why not BepInEx or MelonLoader

Both generate their interop assemblies with Cpp2IL, which parses `global-metadata.dat` **from disk**.
This game's identifier string table is encrypted on disk, so that parse produces garbage.
The il2cpp runtime decrypts the names in memory during startup - the player log prints real names like `GameCreator.Runtime.Common.ShortcutMainCamera` - and `GameAssembly.dll` still exports all 254 il2cpp C API functions.

So this fix resolves every class, method and field **by name, at runtime, through those exports**, and never reads the metadata file.
That sidesteps the encryption entirely.

## Install

1. Download [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) (x64) and put its `winmm.dll` next to `AtelierReslerianaRW.exe`.
   `version.dll` and `winhttp.dll` also work - all three are imported by `UnityPlayer.dll`, confirmed with `dumpbin /imports`.
   Do **not** use `dinput8.dll`: this game never loads it, which is the one place Lyall's Atelier Yumia setup does not carry over.
2. Copy `AtelierReslerianaFix.asi` and `AtelierReslerianaFix.ini` into the same folder.
3. Launch the game and check `AtelierReslerianaFix.log` in that folder.

The game folder is typically `C:\Program Files (x86)\Steam\steamapps\common\AtelierReslerianaRW`.

## Turning it off

Set `Enabled = false` under `[General]` in the INI. The plugin still loads but installs nothing, so the game runs exactly as it would without it.

Prefer that over switching off individual sections. `NudgeElements` is calibrated for the widened canvas, so leaving it on while the resolution returns to 16:9 pushes the panel out the other way.

To remove the mod from the game entirely, delete `AtelierReslerianaFix.asi`, `AtelierReslerianaFix.ini` and `version.dll`. Renaming the `.asi` to any other extension also works, since the loader only picks up `*.asi`.

## Configuration

`AtelierReslerianaFix.ini` is documented inline. What the defaults do:

- Force the primary monitor's desktop resolution in borderless mode, because the game's own options list omits ultrawide modes. Set `Resolution.Enabled = false` to keep the game's own choice.
- Pin the UI cameras to a centred 16:9 band so the HUD keeps its 16:9 layout, and never pin the cameras named in `SceneCameraNames`, so the 3D view keeps the full width.
- Correct the canvas scale, because the game sizes its UI reference as `2160 x screen aspect` and so inflates every root canvas by `screenAspect / 16:9` on an ultrawide screen.
- Leave FOV alone, since Unity's vertical-FOV projection is already Hor+.
- Leave aspect spoofing off: it did not fix the inflation and it made the options screen misreport the resolution.

## Build

Requires CMake 3.25+ and MSVC with a C++23 toolset.
safetyhook, spdlog and inipp are fetched automatically.

```
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

Output: `build/Release/AtelierReslerianaFix.asi`.

## Status

Verified in game at 3440x1440:

- Resolution forced to the full ultrawide, applied once and holding.
- 3D scene fills the whole screen in field and battle.
- HUD locked to a centred 16:9 band, measured at 440..2999 of 3439 px, matching a native 16:9 reference capture exactly.
- Root canvases corrected from `5160x2902` back to their `3840x2160` design size, so UI elements are their intended size.
- Character portraits render with correct proportions.

The default is `Mode = off`: no HUD lock, a full-width picture with no black bars, and element sizes still corrected. It is the configuration verified end to end, and the one `NudgeElements` is calibrated for.

`Mode = viewport` is the alternative, and the only mode that truly locks the HUD to 16:9. Its cost is that in-game full-screen menus get black bars at the sides, because their backdrop is a UI element drawn by the very camera that has to be narrowed. Switching to it means clearing `NudgeElements`, whose offset is derived from the wider canvas that `off` produces.
`Mode = anchors` was an attempt to have both, and it does not work on this game - see [docs/IMPLEMENTATION.md](docs/IMPLEMENTATION.md) for what it did and why.
