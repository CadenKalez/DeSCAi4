# Audio recovery — engine integration guide

This file is the contract between the recovered audio code (`src/audio`, `src/rz`, `src/gz`) and the
rest of the game engine. It lists, per subsystem: what to construct, which **host services** the
engine must provide, the **object-lifetime rules**, every place where the port deliberately
**deviates** from the original binary (and why), and the **quirks of the original** that callers must
not "fix" by accident. Keep it updated whenever recovered behaviour changes.

Conventions
- Addresses are from `Sim City 4 Deluxe Editionsub` 1.2.1 (x86_64). `@ 0x...` in source comments.
- `cIGZUnknown` objects use the original refcount rules: **objects are created with count 0**; the
  first `AddRef` adopts them. `Release()` on most classes destroys the object when the count is
  **<= 1** (not 0), so never call `Release` on an object you did not `AddRef` / adopt.
- All tests: `scripts/run_tests.sh` (ASan + UBSan, clean rebuild). Ledger: `notes/ledger.csv`.

---------------------------------------------------------------------------------------------------
## 1. Freshness (adaptive music) — `src/audio/Fresh*`

Classes: `cFreshCell`, `cFreshScore`, `cFreshCellPlayer`, `cFreshPlayer`, `cFreshDimensionPlayer`,
COM table in `FreshnessDirector.h` (director id `0xfd53ff17`, class ids in `HitClassIds.h`).

### Host seam — `Fresh::cIFreshHost` (`FreshHost.h`), set once with `Fresh::SetHost()`
| method | replaces (original) | notes |
|---|---|---|
| `HasSoundSystem()` | `RZ::SS() != nullptr` | engine returns true once the sound system exists |
| `IsSoundSystemInTestMode()` | sound system slot `+0x188` (`GetTestMode`) | dimension player is inert in test mode |
| `CreateSndByFileName`, `CreateSndByResourceId` | sound manager slots `+0x90`/`+0x88` | result is AddRef'd; the cell player releases it |
| `AddUpdateTarget(target, 250)` / `RemoveUpdateTarget` | sound system `+0xf0`/`+0xf8` | engine must call `target->Update(ms)` every 250 ms |
| `ReadScoreText(key, out)` | resource manager + DB record file adapter | used by `cFreshPlayer::InitializeFromResourceKey` |
| `CreateFreshPlayerInstance()` | `GZCOM()->CreateInstance(0x3c1cf59e, 0xdc1cf595)` | AddRef'd `cFreshPlayer`; used by the dimension player |

`Fresh::GetHost()` may be null; every engine call checks it (original checked `RZ::SS()/SM()`).

### Rules
- One process-wide lock (`cFreshPlayer::Section()`) guards all players; `Update()` is called from the
  sound-system thread, public methods from the game thread.
- A cell's file name is either a plain file (only the last path component goes to the sound manager)
  or contains `...<8 hex digits>.<ext>` = a sound **resource id** (taken from the last 8 chars
  before the first `.`).
- Positions are 0..1000 and are scaled onto the score grid; selection area = rows/columns within the
  radii around the position (clamped to the grid).
- `QueryInterface` of `cFreshPlayer` accepts **only** `kGZIID_cIFreshPlayer` (not id 1).
- `cFreshDimensionPlayer::FadeVolume` records two flags and does nothing else (as in the binary).

### Deviations
1. `cFreshPlayer` takes **its own reference on its score** (original stores a raw pointer and
   `delete`s it, double-freeing if a dimension player's score map released first).
2. `cFreshDimensionPlayer::Init()` stores a bool instead of a counted `RZ::SS()` pointer.
3. Score `ParseScore` is bounds-safe; the original `sscanf`s into fixed buffers.

---------------------------------------------------------------------------------------------------
## 2. Hit manager — `src/audio/HitMan.*`, `HitManServices.h`

`cHitMan` (service id `0x1b2ce671`, interface `cIHitMan` iid `0xbb1bd9a8`) is the root of the Hit
engine: sem registry, track data blocks, control groups, duck map, channel/track-player factory.

### Object layout facts the engine may rely on (asserted in `audio_hitman_test.cpp`)
- `cIHitMan*` is `this + 0x28`; `cRZUnknown` at `+8`; `sizeof(cRZSystemService) == 0x28`.
- `QueryInterface(1)` and `QueryInterface(0xbb1bd9a8)` both return the **cIHitMan** pointer.
  `0x287fb697` returns the `cIGZSystemService` pointer. Service priority is `499999`;
  `GetServiceTickPriority()` through the interface returns 0 (`0x7a116` through the const overload).

### Host seam — `Hit::cIHitManServices` (`HitManServices.h`), set with `Hit::SetServices()`
| method | replaces | notes |
|---|---|---|
| `HasSoundSystem`, `EnumerateUpdateTargets` | service `0x23cecba0` slot `+0x100` | `UpdateView()` enumerates all update targets and calls `UpdateView` on those that implement `cIGZSndViewUpdateTarget` |
| `GetPersistResource` | resource manager `+0x18` | hit lists (`GetHitList`) |
| `CreatePropertySetResource` | `new cHitPropertySet(Track)Resource` + `RegisterResource` (`+0x38`) | three resources created in `Init`; defaults in `HitTables.h` (byte-exact) |
| `ReadPersistRecord` | resource manager `+0xf8`, DB segment `+0xa0`, record file adapter | hit files embedded in packages |
| `ReadFileBytes`, `ApplicationDirectory`, `DataDirectory` | `cRZFile`, `RZ::ADir()`, `RZ::DDir()` | |
| `ParseDebugInfo` | service `0xfbc9cd04` slot `+0x48` | called after each hit file is parsed |
| `CreateTrackPlayer`, `CreateChannel` | `new cHitTrackPlayer` / `new cHitChannel` | |

`RZ::HM()` (used by `cHitList`, the sems) resolves to the manager registered with
`RZ::SetHitMan()`; `cHitMan::Init()` registers `this` when none is set.

### Lifetime / ownership rules
- `Init()` registers the two default sems (`2D default` id `0x7dcded2d`, `3D default` `0x7dcded3d`)
  and creates the property-set resources. Calling it again is a no-op. `Shutdown()` drops sems,
  data blocks, DB segments, sequence sounds, resources, the active list and the source-data handler;
  `Init()` can be called again.
- **Data blocks:** `RegisterTrackDataBlock` keeps a reference and puts the block first, so a newer
  block shadows older entry points. Buffers that `ParseHitFile` allocated are freed on
  `UnloadHitFile`/`Shutdown`/destruction. *Port deviation:* the original frees the buffer of every
  block it drops, including foreign ones; the port frees only what it allocated itself, so foreign
  buffers are never freed by the manager (no double free, possible leak if the registrant expected it).
- **Control groups** are created on demand by `ControlGroup(id)` with refcount 1 owned by the manager
  (original leaks them; the port releases them in the destructor). The pointer returned is *not*
  AddRef'd.
- `AllocChannel` always asks for `cIGZSnd` (iid `0x206077d2`), ignoring the `iid` argument.
  `FreeChannel` only releases channels that `AllocChannel` produced.

### Quirks of the original that are preserved (do not "fix")
- `LoadHitFile` refuses a file whose name equals a loaded block's *stored* name, which is the
  **upper-cased, absolute** path — so the guard only triggers for that exact spelling; any other
  spelling loads a second block. `UnloadHitFile` upper-cases/absolutises (data directory) and removes
  the first block with that name; it always returns true.
- `FilepathHasEmbeddedKey` looks for three 8-hex-digit numbers (type, group, instance; none may be 0);
  the last digit of a number also counts as the first digit of the next, so contiguous digit runs
  overlap by one.
- `AddToActiveTrackList` first **pops the front** (`RemoveFromActiveTrackList` ignores its argument),
  so the list holds only the most recently added track. Do not use it as a registry of all tracks.
- `IsPaused()` reads a flag nothing ever sets (always false); `Pause()` only clears the duck map.
- `DuckMapSetSndobPri` writes the maximum priority into register `0x1b` and refreshes views only when
  the map actually changed.
- `Random(upper, lower)` = `lower + rand(upper - lower)`; `cHitList` calls it as `Random(range, 0)`.

### Deviations
1. `Get/SetRegisterVal`: the binary dereferences a property set at `+0x80` that is never assigned.
   The port stores registers in a table (missing = 0), which is the intended contract.
2. `TrackDataEntryPoint`/`AddToControlGroup` call `this` directly instead of looking the service up
   again through the registry (identical for the single instance).
3. `RegisterTrackDataBlock` releases its temporary `QueryInterface` reference *after* the list took
   its own (order matters for count-0 objects).
4. Hit-file scanning is bounds-checked (original may read up to 3 bytes past the buffer); the dead
   "TRAX" scan was dropped.
5. The unused members (`std::map<cRZString,int>` at `+0x60`, hit-list map at `+0x150`, critical
   section at `+0x38`) are not modelled.

---------------------------------------------------------------------------------------------------
## 3. Control groups — `src/audio/HitControlGroup.*`
`cHitControlGroup` implements `cIHitControlGroup` (`AddTrack`, `AsIGZSnd`) plus the whole `cIGZSnd`
surface as stubs; only id, volume (default 1024), name and the track set are real. It only answers
`QueryInterface` for its own iid `0xfb1c0050` and id 1 (not `cIGZSnd`'s) — use `AsIGZSnd()`.
`Release()` destroys it when the count is < 2.

---------------------------------------------------------------------------------------------------
## 4. Tooling invariants
- `scripts/ledger_class.py`, `ledger_rename.py`, `check_unique_names.py`: every recovered function has
  a class-unique descriptive name in Ghidra and in `notes/ledger.csv`.
- `scripts/run_tests.sh` must pass before every commit.
