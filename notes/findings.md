# SimCity 4 (Aspyr macOS / Steam) — binary findings

Source: `Sim City 4 Deluxe Edition.app`, version **1.2.1 (build 185949, steam)**, Aspyr Media, © 2020.
Bundle id `com.aspyr.simcity4.steam`. Built with Xcode 9.4.1 (9F2000), SDK macosx10.13, clang.
Minimum macOS: **10.12.6** (`LSMinimumSystemVersion`; Mach-O `LC_VERSION_MIN_MACOSX` 10.12).

## Bundle layout (Contents/)
| Item | Size | Notes |
|---|---|---|
| `MacOS/Sim City 4 Deluxe Edition` | 981 KB | Aspyr "GameGuide" launcher (ObjC, x86_64, not stripped; 395 syms). Starts the `sub` binary. |
| `MacOS/Sim City 4 Deluxe Editionsub` | 22 MB | **The actual game.** x86_64, 253,607 symbols. |
| `MacOS/libsteam_api.dylib` | 69 KB | Steamworks; x86_64, weak-linked via `@loader_path`. |
| `Resources/` | 6.5 MB | Launcher images/fonts, scripts, plists, **`SC4SimGrid.cpp.inl`** (leftover Maxis source, see below). |
| `GameData/` | 1.2 GB | DBPF `.dat` archives, video, audio, regions, fonts — indexed, not copied. |
| `Frameworks/` | — | Does not exist; nothing bundled besides libsteam_api. |

## Architecture / symbols
- **x86_64 only** (thin Mach-O, MH_EXECUTE, 41 load commands). No arm64 slice, no i386, no ppc. Runs on Apple Silicon only through Rosetta 2.
- **Not stripped.** 253,607 nlist entries (251,964 local, rest external/undefined); ~61.7k defined via `nm -U`.
- **~49.5k mangled C++ symbols** (`__Z…`), 43.3k text symbols → `notes/symbols-functions-demangled.txt`.
- **RTTI intact:** 1,636 `typeinfo` and 1,318 `vtable` symbols → `notes/vtables-demangled.txt`, `notes/typeinfo-demangled.txt`.
  - 464 `cSC4*` vtables, 201 `cGZ*`, 77 `cIGZ*`, 58 `cRZ*` (Maxis/EA GZ + RZ frameworks).
- ObjC runtime metadata present (≈70 `OBJC_CLASS`/`METACLASS` symbols) — SDL/Cocoa glue, Steam init (`initSteam`, `isSteamBuild`).
- **No DWARF** (no `__DWARF` segment, no .dSYM). Names but no types/struct layouts.
- Compiler: Apple clang (Xcode 9.4.1). C++ exceptions present (`__gcc_except_tab`, `__eh_frame`, `__unwind_info`); static constructors in `__mod_init_func`.
- Linked frameworks: OpenGL/AGL (renderer), CoreAudio/AudioToolbox/AVFoundation, Carbon, Cocoa, WebKit, IOKit/ForceFeedback, StoreKit, Steam.
- Entry-adjacent functions seen: `MacRequestQuit(cGZFramework*)`, `SDLHandleEvent(cGZFramework*, SDL_Event*)`, `GetIntroScreen()`, `PostPseudoKeyEvent(...)` — SDL-based port layer (Aspyr's `cGZCanvasW32`-style Win32 shims on top).

## Interesting leftovers
- `Resources/SC4SimGrid.cpp.inl` — an original Maxis source file (template `cSC4SimGrid<T>`) shipped in the bundle by accident. Useful as ground truth for the SimGrid class and for Maxis coding style (include names: `SC4SimGrid.h`, `SLAccessor.h`, `RZXError.h`, `ISC4City.h`, `SC4LoadSaveHelp.h`). Treat as reference only; it is copyrighted and is gitignored with game-binaries/.

## Process notes
- Launcher is a separate x86_64 Mach-O; Steam is configured to run the `…sub` binary directly (`SteamDefaultPlayExecutable`).
- Code-signing identifier of launcher: `File Doesn't Exist, Will Create: /Users/jenkins/build/workspace/01/SimCity4/build/OSX.STEAM/Sim` — a Jenkins build path leaked into the signature.
