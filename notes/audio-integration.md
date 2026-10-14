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
## 4. Property sets and typed values — `src/gz/cGZAny.h`, `cGZPropertySet.*`
`cIGZAny` value classes (`AnyUint32` id `0xeb61e4f7`, `AnyFloat32` `0xabc78708`, `AnyBoolean`
`0xcba908e1`, `AnySint32` `0x0c264712`, `AnyString` `0x0b8bea18`; object size 16) and the uint32-keyed
property set (iid `0xebb10cdd`).
- **No type conversion:** `Value(key, &any)` fails (and leaves `any` untouched) when the stored type
  differs from `any`'s type. Hit registers are written with `AnyUint32` (`SetRegisterValue`) or
  `AnyFloat32` (`SetRegisterFloat`); reading a float register with `GetRegisterValue` returns 0.
  Engine code that stores audio properties must use the same type the reader uses.
- `SetValue` replaces the stored value even when the type changes; iteration is in key order.
- `AnyString` copies share one reference-counted `cRZString`.
- `CompareTo` compares the 8 payload bytes as signed chars (padding included; the port zeroes it).

---------------------------------------------------------------------------------------------------
## 5. Channels — `src/audio/HitChannel.*`
One voice. Created through `cHitMan::AllocChannel` (host `CreateChannel`), released with
`FreeChannel`. Object layout asserted in `audio_hitchannel_test.cpp` (`sizeof == 0x78`, interface
offsets `+0/+8/+0x10/+0x18/+0x20/+0x28`). `QueryInterface` answers six iids and **not** id 1.

### Host services used (via `Hit::cIHitManServices`)
`GetDefaultSndPlayerClassId(is3D)`, `CreateComInstance(classId, 0x206077d2)` (underlying sound),
`GetPersistResource(key{0xebfee345,0xfc45caf7,instance}, 0xebb10cdd)` (parent property sets).

### Register file
- Layers: own set first, then inherited parents (built when register `0xff4c97be` is written: its
  value is the *instance* of the parent property set in group `0xfc45caf7`, chained until a parent has
  no link or is missing). Reads search layers front to back; writes/`Has`/`Clear` use the own layer.
- Legacy numbers (< 0x100) are translated through `nsHitReg`; **`ClearRegisterValue` does not translate**
  (binary bug, kept) so clearing a legacy number clears the untranslated key.
- Writing a register notifies unless the caller passes `suppressNotification = true`:
  `0xff90aac8/0xff53dbf3` volume & scale, `0xff3ec880` pan, positions/velocities, min/max distance
  -> `UpdateVolPan`; `0xff099db2` frequency, `0xff3071b9` pitch multiplier -> `UpdatePitch`;
  `0xff0150f2` rolloff; `0xff734343/0xffd3d39b` dry/effect level; `0xffbdb196/0xffa42951` low/high
  pass; `0xff86fe79` time multiplier; `0xffb36659` pause the underlying sound; `0xffaec905` drops the
  data source; `0xff0ad592` is forced to 0 while no note plays.
- Effective volume = `volume * volumeScale / 1024` truncating toward zero (`+0x34`).

### Quirks kept
- `Init()` **resets the reference count to 0** (it is not a no-op) — call it before the first AddRef.
- `SetAll` writes the *velocity-z* value into position-z (binary bug); `Get/SetAll` otherwise sane.
- `GetMinimumDistance/GetMaximumDistance(float*)` do not write the pointer; they return the register's
  low byte. Use `GetRegisterValue(0xffaab623 / 0xffa6a614)` for the value.
- `SetPosition/SetVelocity` truncate floats to int registers.
- `UpdatePitch` returns the *inverse* of the sound's `SetFrequency` result when a frequency register
  exists (as compiled); it needs the sound to be loaded.
- `Stop()` is `Kill()`; `FadeVolume`, `SetEffects*`, `SetName`, `GetName`, `SetSoundFile*`,
  `StopAllInstances`, `GetUseCount` are stubs.

### Deviations
1. `0xff0ad592`: the binary re-enters its own notification forever when the value is non-zero and no
   note plays; the port clears it once.
2. Parent-chain walk is limited to 64 hops (the binary has no limit; a cyclic chain would hang).
3. A channel whose sound could not be created tolerates the missing data-source holder (the binary
   dereferences it).

---------------------------------------------------------------------------------------------------
## 6. Tooling invariants
- `scripts/ledger_class.py`, `ledger_rename.py`, `check_unique_names.py`: every recovered function has
  a class-unique descriptive name in Ghidra and in `notes/ledger.csv`.
- `scripts/run_tests.sh` must pass before every commit.
