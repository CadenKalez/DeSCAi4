# SC4-Decomp

<!-- progress:start -->
![decompiled](https://img.shields.io/badge/decompiled-65.0%25-yellow)

`█████████████░░░░░░░` **65.0%** — 17,475 of 26,880 game functions recovered, 9,405 remaining (details: [notes/progress.md](notes/progress.md))
<!-- progress:end -->

Reverse-engineering / decompilation workspace for **SimCity 4 Deluxe Edition (Aspyr macOS port, Steam, v1.2.1 build 185949)**.
Analysis is done in Ghidra, driven by Claude Code through a Ghidra MCP server.

The game binary is **x86_64, not stripped, with intact C++ RTTI** (~49.5k mangled symbols, 1,318 vtables). See `notes/findings.md`.

> Game files are copyrighted and are **never committed**. Only my notes, scripts and original code go into git.

## Layout
```
SC4-Decomp/
├── game-binaries/      read-only COPIES of code from the Steam bundle (MacOS/, Info.plist, small Resources) — gitignored
├── game-assets-index/  listings/sizes/hashes of GameData (1.2 GB, not copied)
├── ghidra/projects/    Ghidra project "SC4" (SC4.gpr + SC4.rep) — gitignored
├── mcp/
│   ├── ghidra-mcp/     clone of bethington/ghidra-mcp @ v7.0.0-rc.2
│   ├── downloads/      release zip (Ghidra plugin) + bridge wheel
│   └── venv/           Python 3.12 venv with the bridge (bridge-mcp-ghidra)
├── notes/              findings, hashes, Info.plist dump, symbol/vtable dumps
├── scripts/            ghidra-gui.sh, ghidra-headless.sh, check-mcp.sh, dump-symbols.sh
├── README.md
└── SETUP-LOG.md        every command + versions
```
The original Steam install is never modified:
`~/Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app`

## Toolchain (Homebrew)
Ghidra 12.1.4 (`/opt/homebrew/opt/ghidra`), OpenJDK 21.0.12 (keg-only `openjdk@21`), GhidraMCP plugin 7.0.0 (rc.2), bridge 7.0.0, mcp 1.30.0.

## Reopen everything
1. `scripts/ghidra-gui.sh` — launches Ghidra. Open project **SC4** (`ghidra/projects/SC4.gpr`).
2. Make sure **GhidraMCPPlugin** is enabled (one-time, see checklist below). Open `Sim City 4 Deluxe Editionsub` in the CodeBrowser (double-click).
3. `scripts/check-mcp.sh` should print a connection OK from `http://127.0.0.1:8089`.
4. Start Claude Code **in this folder** (`cd ~/Downloads/SC4-Decomp && claude`). The MCP server `ghidra-sc4` is registered for this project (local scope).
   Check with `claude mcp list` → `ghidra-sc4 ✔ Connected`. Note: "Connected" only proves the bridge started; the Ghidra backend must also be running (step 3).

## One-time GUI checklist (needs clicks)
1. Run `scripts/ghidra-gui.sh`. Accept the license/first-run prompts.
2. In the **project window**: *File → Open Project…* → pick `~/Downloads/SC4-Decomp/ghidra/projects/SC4.gpr`.
3. *File → Install Extensions…* — **GhidraMCP** should already be listed and ticked (I placed it under `~/Library/ghidra/ghidra_12.1.4_PUBLIC/Extensions/`). If it is not listed, click the green **+** and choose `mcp/downloads/GhidraMCP-7.0.0-rc.2.zip`. Restart Ghidra when asked and re-open the project.
4. In the project window: *File → Configure…* → click **Utility** → tick **GhidraMCPPlugin** → OK → Close. Save the tool on exit.
5. Check *Tools → GhidraMCP → Server Status* shows port **8089** and TCP enabled (or `scripts/check-mcp.sh`).
6. Double-click `Sim City 4 Deluxe Editionsub` to open it in the CodeBrowser. When Ghidra asks **"Analyze now?" answer NO** until the analysis plan in SETUP-LOG.md is agreed.

## Ghidra project state
Project `SC4` contains `Sim City 4 Deluxe Editionsub` — loader *Mac OS X Mach-O*, language `x86:LE:64:default`, compiler spec `gcc`. **Imported only; auto-analysis has NOT been run.**
Frameworks (`/System/Library/...`) are unresolved externals — expected, there is no dyld shared cache for x86_64 on this Mac.

## Fallback MCP
If the 7.0.0-rc.2 prerelease misbehaves: LaurieWired/GhidraMCP **2.0** (stable, also built for Ghidra 12.1.4, `uvx --from git+https://github.com/LaurieWired/GhidraMCP ghidra-mcp`). Both default to port 8080/8089 differently — run only one plugin at a time.
