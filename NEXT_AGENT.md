# Start-here prompt for the next agent

> **Start with [notes/SESSION_HANDOFF.md](notes/SESSION_HANDOFF.md)** - it is the state of the work at the end of the
> 2026-10-23 session: what is finished, what is explicitly not, the highest-value next moves, and the traps that cost
> time (the unreliable Ghidra MCP, the ledger_upgrade partial bug, and two real bugs in the recovered city-load path).


Paste this as your first message / system prompt:

> You are continuing a hand-written clean C++20 decompilation of SimCity 4 (Aspyr macOS/Steam v1.2.1, x86_64)
> in /Users/fernandojosesampaio/Downloads/SC4-Decomp. Read AGENT_RULES.md, notes/progress.md and `git log -20`
> first. Do not stop after one class: loop (pick the next class -> export -> implement -> test -> ledger ->
> commit) until told to stop, and give a short progress report each time you stop.

## Rules (summary; full text in AGENT_RULES.md)
- Hand-write every function, unique descriptive names, record in notes/ledger.csv; zero duplicates.
- Commit locally after each class, never push. End messages with
  `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`.
- Steam folder is read-only; never commit game binaries, assets or Ghidra DBs.
- Ask before sudo, downloads >500 MB, or touching anything outside SC4-Decomp / Homebrew.
- Never edit scripts/run_tests.sh while it runs. Heap-allocate COM-style objects in tests.
- Environment: arm64 macOS, zsh, CLT only (no `timeout`, `sed -i ''`), tests use ASan/UBSan.

## Per-class loop (the "skill")
1. `python3 scripts/export_class.py <gz|sc4> '^cSC4Name$'` -> read `work/<group>/cSC4Name.txt`;
   `scripts/vtable.py`, `scripts/dec_addrs.py <addr>` for slots/single functions.
2. Write `src/<group>/Name.{h,cpp}` in the style of neighbours; replace `SL::sp*` globals with small host
   interfaces; keep original quirks, comment them.
3. Test in `scripts/tests/` + entry in `scripts/run_tests.sh`; prefer oracle (Ghidra output -> C, compare on
   random inputs: scripts/run_bit1_oracle.sh) or real game data.
4. `python3 scripts/ledger_class.py --class X --source 'src/..|h' --evidence '...' [--partial 0xADDR]`
   (free functions: `scripts/ledger_add.py addr:UniqueName`), then `python3 scripts/check_unique_names.py`.
5. Register and verify (do not skip; details in AGENT_RULES.md "Definition of done"):
   `python3 scripts/list_seams.py` -> add `todo.py add ...` rows for every follow-up and replace TODO comments with row ids ->
   `python3 scripts/verify_class_done.py <Class...>` must print `ok` (use `--fix` for the generated files only).
6. Commit.

## Current state / next targets
> **STALE WARNING:** the lists below were written before many classes were finished (cGZWinTextEdit, cGZWinListBox,
> cSC4FireOccupant, cSC4App, cSC4BudgetSimulator, ... are done). Always check `notes/ledger.csv` before picking a class; see the
> "Parallel-agent rules" in AGENT_RULES.md.
- Status at handoff: 10,146 / 28,371 game functions recovered (35.76%); 89/89 tests pass
  (run via a repo-local snapshot copy of scripts/run_tests.sh, delete it afterwards; full run takes ~5 min).
- Shortcuts: `/tmp/show.py` and `/tmp/first.py` were session-temporary helpers (print decompile blocks by
  address / first body of each function from work/<g>/<Class>.txt); recreate if needed. Disassembly for ambiguous
  float/SIMD code: `otool -arch x86_64 -tV <game binary>`. zsh does not word-split `$var`; use `${=var}`.
- Seams used: Lua C API (`src/lua/Lua5Thread.h` ILuaApi, test fake `scripts/tests/lua_minilua.h`), protection
  simulators (IProtectionHost), occupants over `cSC4Occupant` (`src/sc4/Occupant.h`), stream helpers in
  `gz/StreamOps.h`, `SC4Read/WriteGZSerializable` (needs a DB segment stream; plain memory streams skip nested objects).
- Done this session: Lua stack (cGZLua5Thread, cGZLua5, thread registry/coroutine lib, Lua4 compat, cSCLua),
  cSC4FireProtectionSimulator, Foundation/Landfill/PowerLine/Toxic occupants, cGZAlphaBlt, cSC4RegionalTerrainMap,
  cRZRefCount, cGZApp, cSC4AdviceSubject.
- Partials left: cSCLua Game Data Registry bridge (needs cISC4GameDataRegistry), Fire sim Init/streams/DoMessage,
  cGZLua5::default_error_reporter (output compiled out), nsGZLua5ThreadLib needs the patched Lua (state[-1] slot).
- Skipped for missing bases: cSC4SeaportDeveloper (cSC4PortDeveloper/cPortLot), cSC4DisasterManagerFire
  (cSC4DisasterManager), Pipe/Subway/UndergroundConnection (cSC4UndergroundOccupant), NetworkTunnelOccupant
  (cSC4NetworkOccupantWithPreBuiltModel). Recovering those bases unlocks ~300 functions.
- Good next targets (by remaining count): cGZWin* UI family (TextEdit 235, Grid 210, cGZWin 183, ListBox 165),
  cSC4App 223, cSC4MySim 211, cSC4Automaton 189, cSC4NetworkTool 196, cSC4FireOccupant 61, cSC4TextDecalOccupant 69,
  cSC4TrafficNetworkMap 97, cSC4BudgetSimulator 87, cGZDBSegmentDirectoryFiles 88, free utilities (5.7k "(free)").
- Playable client: src/app/main.mm (Metal, scripts/build_app.sh). Still missing: zoning/placement, click-to-inspect,
  simulators driving growth. Skip DX7 emulation per notes/graphics_scope.md.

## Update (latest session end)
- Honest progress (scripts/progress.py "Recovered so far") is ~9,400 / 28,371 game functions (~33%). Goal 50% = 14,186.
- Done since last note: NeighborConnection, 16 occupant filters, 20 ordinances (OrdinanceTypes), DX7 stub + DX7Types.h, 16 ambience state filters, GZ input messages, GZ allocators, cGZDrawContext (state/clip/transform; Rasterize*/Tesselate*/Direct* left partial), cGZWinFlatRect.
- Helpers (in /tmp, recreate if gone): cls.py (print a class's decompile without thunks/dtors), led.sh (ledger classes + progress + commit), runone.sh (build/run one registered test). scripts/tests/fake_winmgr.h is a shared fake window manager for control tests.
- Lesson: Ghidra drops message construction in GZOnKey*/forwarders; check /tmp/sc4.asm (otool -arch x86_64 -tV) for those.
- Next: other cGZWin* controls (BMP, Knob, Text, Slider, Scrollbar, Spinner, Combo) on top of cGZWin + cIGZDrawContext; then CDirect3DVertexBuffer7/CDirect3D7 (need a GL seam); cSC4RegionalCity.
- Pick cheap classes with the avg-size ranking (gap between symbol addresses) rather than by name.

## Update (cSC4TrafficSimulator session)
- Done: cSC4Demolition region sweep/terrain/effects + test (2d7463a); TrafficSimulator subnetwork queries (e79d01e): ClearSubnetworks,
  GetSubnetworksForLot/InRectangle/InRegion, AreLotsConnected, GetConnectedOccupantCount/DestinationCount, GetOccupantCountForAllSubnetworks.
  NetworkInfo +0xc is now `subnetwork[4]` (ushort ids); published vector +0xc0, working vector +0xa8 (WorkOnUpdateSubnetworkInfo copies).
- Run single tests with `zsh scripts/runone.sh <name>` (not bash: it silently runs a stale binary).
- Next in TrafficSimulator (nothing written yet): neighbour-connection tables. Plan: 4 border vectors of {u16 mask, u16 pool, f32 capacity}
  (+0xf0 west[z], +0x108 north[x], +0x120 east[z], +0x138 south[x]; selected by x<0 / x>lastX / z<0 / else); pool map<u16, property holder> at
  +0x150 with default pool +0x168; edge-flag bytes at [x+1][z+1] (+0x80) and 4 counters +0x31c. Functions: AddNeighborConnection 0x1000820ac,
  RemoveNeighborConnection 0x100082358, RemoveUnusedPools 0x1000828a2, GetNeighborConnection 0x100089830, GetTrafficNeighborConnection 0x10008a090,
  GetNeighborPoolPropertyHolder 0x1000884e8, CapacityStillAvailable 0x100087c06. Needs a host seam over spNeighborsSim (slots 0x58 connection
  lookup, 0x70 -> property holder) and tables kValidNeighborConnectionTypesForTransitNetworkType = {0x200,0x170c,0x2,0x40,0x80,0x800}
  (read at 0x100a16330 with scripts/macho_read.py); kDX/kDZ at 0x100a162c0/d0, kOppositeEdge 0x100a163f0, kDefaultTravelTypeForNetwork 0x100a16400.
- After that: FloodSubnetwork 0x10008c4e4 / UpdateSubnetworks / UpdateSubnetworkInfo (needs per-cell connection masks at +0x98, 0x2e bytes/cell,
  tunnel map), then the Work* trip pipeline, ferries, Init/Read/Write. Open items: ModifyTerrainHeight's extra `long long*` parameter,
  DoDemolishGroupEffect test.

## Seams
`docs/SEAMS.md` lists every host/seam interface (generated by `scripts/list_seams.py`) and the hook-up checklist for cSTETerrainView3D. When you add a seam or finish the class behind one, regenerate it and update the table.

## To-do registry (notes/todo.csv, scripts/todo.py)
Finishing a class is not the end: add its follow-ups (neighbours to recover, seams to hook up, verifications) with
`python3 scripts/todo.py add ID Class "task" --blocked-by ID,ID --where path` in the same commit. Pick work with
`python3 scripts/todo.py list --ready`; close items with `todo.py done ID`; `todo.py sync-ledger` refreshes the auto rows for
recovered-partial / pending functions and `todo.py sync-classes` the CLASS-<name> rows (one per unfinished class, blocked by its
unfinished base classes from `notes/class_bases.json`; rows close themselves at 0 remaining). Run both after each class. Terrain view items are TERRAIN-VIEW-*, terrain model items TERRAIN-*.

## Update (sparse-matrix + sprite session)
- Done: cRZSparseMatrix<T> (39 symbols, src/rz/SparseMatrix.h + test; unblocks the grid and tree view), cS3DModelInstanceAnimatedSprite
  (54 symbols, src/s3d/ModelInstanceAnimatedSprite.{h,cpp} + test; salvaged, its test had failed on raw-vs-derived table counts,
  unsigned row/column offsets, a SetFrame accumulator assumption and the camera reference count).
- Progress: 16,388 / 26,880 game functions (60.97%). notes/SC4_decompilation_status.xlsx has the per-class breakdown,
  regenerate with `scripts/progress_workbook.py` (python3, needs openpyxl).
- Next: GRID-1/GRID-2 - cGZWinGrid (189 functions) on top of cRZSparseMatrix. The decompilation is already exported in
  work/gz/cGZWinGrid.txt (9.4k lines); the object is 0x3c8+ bytes, three cRZSparseMatrix members at +0x238/+0x268/+0x298,
  cells addressed through cIGZWinGrid::CellData. Start with the data model (cells, formats, rows/columns), leave GZPaint
  (4.3 KB) and the mouse/sort paths for the end.
