# SETUP-LOG

Date: 2026-10-13. Host: MacBook arm64 (Apple Silicon), macOS 27.0.1. Free disk at start: ~15 GB.
Steam folder was only **read** (ls, file, nm, otool, strings, cp from, shasum); nothing there was modified.

## Versions
| Component | Version | Source |
|---|---|---|
| Game | SimCity 4 Deluxe (Aspyr) 1.2.1, build 185949 steam | Steam |
| Ghidra | 12.1.4 PUBLIC (20260921) | `brew install ghidra` (bottle 464 MB, installed 792 MB) |
| JDK | OpenJDK 21.0.12.1 | Homebrew `openjdk@21` (already installed, keg-only) |
| GhidraMCP plugin | 7.0.0 (release v7.0.0-rc.2, **prerelease**) | github.com/bethington/ghidra-mcp |
| Bridge | ghidra-mcp-bridge 7.0.0, mcp 1.30.0, pydantic 2.14.1 | release wheel into venv |
| Python | 3.12.14 | Homebrew (`/opt/homebrew/bin/python3.12`) |
| Already present | git 2.55.0, cmake 4.4.3, llvm 22.1.8 (llvm-objdump), uv, Homebrew 7.0.9, Xcode CLT (otool, nm, c++filt) | — |

## Commands run (in order)
```
ls ~/Library/Application\ Support/Steam/steamapps/common/
find / du -sh on "SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents"
file / lipo -info / codesign -dv / nm / otool -hv -l -L on MacOS/*
nm -U | c++filt   (counts of _Z symbols, _ZTV, _ZTI, OBJC_CLASS)
strings -a on both executables
plutil -p Info.plist
mkdir -p ~/Downloads/SC4-Decomp/{game-binaries,game-assets-index,ghidra,mcp,notes,scripts}
cp -Rp Contents/MacOS, _CodeSignature -> game-binaries/ ; cp -p Info.plist PkgInfo ; cp -p Resources/{*.sh,*.plist,*.inl}
shasum -a 256 (code files -> notes/hashes.txt ; GameData -> game-assets-index/sha256.txt)
ls -lR / du on GameData -> game-assets-index/
nm -U ... | xcrun c++filt -_  -> notes/symbols-*.txt, vtables-, typeinfo-
curl api.github.com (Ghidra release, MCP candidates: repos, releases, commits, issues)
curl ghcr.io (HEAD on Ghidra bottle: 464,258,543 bytes)
brew info ghidra ; brew install ghidra
git clone --depth 1 --branch v7.0.0-rc.2 https://github.com/bethington/ghidra-mcp.git  (mcp/)
curl -L -O GhidraMCP-7.0.0-rc.2.zip, ghidra_mcp_bridge-7.0.0-py3-none-any.whl  (mcp/downloads/)
/opt/homebrew/bin/python3.12 -m venv mcp/venv ; pip install --upgrade pip ; pip install <bridge wheel>
unzip GhidraMCP-7.0.0-rc.2.zip -d ~/Library/ghidra/ghidra_12.1.4_PUBLIC/Extensions/
claude mcp add ghidra-sc4 -s local -e GHIDRA_MCP_URL=http://127.0.0.1:8089 -- <venv>/bin/bridge-mcp-ghidra
JAVA_HOME=<openjdk@21> analyzeHeadless ghidra/projects SC4 -import "<...>sub" -processor x86:LE:64:default -cspec gcc -noanalysis
git init -b main ; git add -A  (nothing committed)
```

## MCP selection (why)
| | bethington/ghidra-mcp | LaurieWired/GhidraMCP |
|---|---|---|
| Stars / activity | 5.6k, commits daily (last 2026-10-13) | 11.2k, last commit 2026-10-10 |
| Ghidra 12.1.4 build | ✔ v7.0.0-rc.2 (**prerelease**); stable v6.0.0 targets 12.1.2 ✘ | ✔ 2.0 (stable) |
| Tools | 209 (rename/retype/struct/enum/batch/emulation, headless) | far fewer, simpler |
| Open issues | 31 (mostly PRs/features) | 89 |
Ghidra extensions need an exact Ghidra version match (LaurieWired issue #170), so only these two builds are viable on 12.1.4.
Picked bethington for struct/batch/rename tooling needed to annotate ~43k functions. Fallback: LaurieWired 2.0.

## Skipped / not done / caveats (plain)
- **rizin/radare2 not installed** (optional; not needed now).
- **No Ghidra analysis has been run.** Waiting for confirmation (plan below).
- **GUI steps not done**: enabling GhidraMCPPlugin and the first live MCP connection are left to you (README checklist). The Claude Code registration shows "Connected" for the bridge process only; the Ghidra backend (port 8089) is not up until Ghidra is running with the plugin enabled. **The live end-to-end tool calls are untested.**
- The MCP plugin is a **release candidate**; I did not build it from source (used the prebuilt release zip).
- Steam `Info.plist`/launcher binaries codesign check printed odd identifiers (a Jenkins build path); harmless.
- Homebrew warned that tap `gcenx/wine` is untrusted (pre-existing; unrelated, untouched).
- `ghidra/` is gitignored; headless import needed explicit `JAVA_HOME` because `openjdk@21` is keg-only (`scripts/ghidra-headless.sh` handles it). `ghidraRun` from brew already sets it.
- Git: repo initialised, files staged, **nothing committed, nothing pushed, no GitHub repo**.
- The `nm`/c++filt dumps in `notes/` are gitignored (derived from the binary, regenerate with `scripts/dump-symbols.sh`). `notes/hashes.txt`, `Info.plist.txt`, `findings.md`, `game-assets-index/sizes.txt` are committable.
- Working-directory note: this session was started in `~/Downloads/BanjoNative`; it was moved to SC4-Decomp at your request. Nothing SC4-related was written to BanjoNative (only scratch downloads in the session scratchpad).

## Proposed analysis plan (awaiting your confirmation)
- Target: `Sim City 4 Deluxe Editionsub` (22 MB, ~43k functions, symbols present).
- Estimated time on M1: **~15–40 min** with defaults; much longer if Decompiler Parameter ID is on (can be 1h+). Ghidra RAM default is fine (~4 GB heap); I'd raise max heap to 8 GB via `launch.properties` (`MAXMEM`) if you agree.
- Analyzers to enable: Mach-O/ObjC (`Objective-C 2 Class`, `Objective-C 2 Message`), `Demangler GNU` (handles `_Z`), `GCC Exception Handlers`, `Function Start Search`, `Non-Returning Functions - Discovered`, `Stack`, `Data Reference`, `Scalar Operand References`, `Shared Return Calls`, `Create Address Tables`, `Embedded Media`, `ASCII Strings`, `Call-Fixup Installer`, `Decompiler Switch Analysis`.
- Leave **off** for pass 1: `Decompiler Parameter ID` (slow), `Aggressive Instruction Finder`, `Variadic Function Signature Override`. Can be run later in a second pass.
- Also recommended before analysis: run Ghidra's *Recover Classes from RTTI* script path (see next steps in the summary) after analysis, not before.
