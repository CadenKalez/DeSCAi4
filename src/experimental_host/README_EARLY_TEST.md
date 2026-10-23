# EARLY TEST — experimental game host (NOT recovered game code)

Everything in `src/experimental_host/` is **new hand-written glue**, an experiment to find out how far the recovered
classes get us towards a window that boots, shows UI and takes input. It is **not** a decompilation of any function and
has **no rows in `notes/ledger.csv` or `notes/todo.csv`**.

For DeepSeek / other agents:
- Do **not** try to "verify", "ledger", "test" or "complete" these files as if they were original functions.
- Do **not** rename or move them, and do not edit them unless the task is explicitly about this host.
- The recovered classes they use (`cGZWinMgrBase`, `cGZWin`, `cGZBuffer`, `cGZDrawContext`...) are the real work;
  anything missing here shows up as a link error or a failed `CHECK`, which is the useful output of the experiment.

What it is made of:
| File | What |
|---|---|
| `HostWinMgr.{h,cpp}` | concrete window manager: the platform half of `cGZWinMgrBase` (`PostWinMessage`, `RunModalLoop`) |
| `HostGraphics.{h,cpp}` | `IWinGraphics` / `IDrawContextHost` / `IBufferHost` over a CPU `cGZBuffer` + the recovered software draw engine |
| `HostFonts.{h,cpp}` | FreeType adapter for the recovered `cGZFont` seams (`IFontHost`/`IFontRasterizer`/`IFontFace`/`IFontGlyph`); needs Homebrew `freetype` |
| `HostBoot.{h,cpp}` | boot_probe's sequence as a function: the recovered `cSC4App` spine against the real game data (`--boot`) |
| `host_main.mm` | Cocoa window + event pump, builds a test window tree, paints it, presents the buffer |

Modes: `--boot` (spine only), `--game` (spine, real splash picture, then the test menu), `--splash`, `--selftest`, `--screenshot f.png`.
The splash is the real PNG from SimCity_1.dat (856ddbac/46a006b0/362b8543) painted by the host: the game's own splash window
(`cSC4WinSplashScreen`) is not recovered, so it is not driven by game code yet.

Build / run: `scripts/build_host.sh && build/sc4_host` (also `--screenshot out.png` for a headless frame).
