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
## 6. Track players — `src/audio/HitTrackPlayer.*`
One running Hit track (a bytecode program). Created with `cHitMan::CreateTrackPlayer` (host
`CreateTrackPlayer`), driven by the sound system's update target (`Update(ms)` every 20 ms while
playing). Subobject offsets asserted in `audio_hittrackplayer_test.cpp`; `QueryInterface` accepts id 1
and `0xbb1be367` for the primary pointer, plus the seven other interface ids.

### Host services (in addition to section 2)
`GetSoundById(id, iid)` (sound manager GetSound), `RegisterUpdateTarget(target, ms)` /
`UnregisterUpdateTarget`, `SetGlobalProperty/GetGlobalProperty/SetInstanceProperty` (property
cache), `EnumerateUpdateTargets` (used by "kill/stop track by id").

### Rules
- `PlayPause()` takes one reference that the track releases when it ends (`end`, `Kill`); callers
  must hold their own reference while the track plays.
- Starting needs register `0xff5f15e1` (entry point key) to resolve through
  `cHitMan::GetTrackDataBlockAndOffset`; otherwise `PlayPause` returns false.
- Execution: instructions run until one waits; a single tick executes at most 101 instructions
  without a wait before "Too many instructions..." is reported (and execution continues).
- Registers: own layer [0], the track definition [1] and its parent chain [2..9]. Writes go to layer 0
  and are copied to dependent sounds that do not define the register themselves.
- Errors are delivered through `cIHitMan::HandleTrackFlowError`; the default handler kills the track
  and sets its error flag (`GetErrorCondition`).

### Opcode set (byte codes; register operands 0..0x4a, immediates little endian 32 bit)
Loads/moves 04 05 06 4a 60 61 1f; arithmetic 10 add, 11 sub, 12 div, 13 mul, 19 abs, 2f inc, 30 dec;
logic 17 not, 29 nand, 2a or, 2b nor, 2c xor, 15/16/28 compare-to-register; compare 0e 14 55;
clamp 2d/2e; flow 07 call, 0a call by entry key, 08 return, 0c end, 0d jump, 20/21 mark jump/save,
3e..43 conditional jumps, 4d..52 wait-until-condition, 09 wait, 0b wait-for-note, 1b stop, 1c
stop-if-zero; notes 01 02 03 0.. 5b 5c; hit lists 23..27 44 5f; groups/ducking 45 46 47 49 53 54;
tracks 32 33 5a; app data 48 59; global properties 56 57; random 18; callbacks 22; throw 58.

### Deviations / quirks
- `AddSource` never adds (the binary's insertion is unreachable); `SourceList()` stays empty.
- Division by zero is reported ("Divide by zero") and yields 0 instead of faulting.
- `0xffc6e666` (target sound) asks the dependent for iid `0xebb10cdd`, which sounds never expose,
  so it always reports "no register interface" — as in the binary.
- `CompletionTime()` returns 0 until the sound data sources are recovered; `operator=` is not ported.
- `HasRegisterValue` looks only at layers 0 and 1; `GetRegisterFloat` does not translate legacy ids.

---------------------------------------------------------------------------------------------------
## 8. Property-set persistence — `src/gz/cGZPropertySetSerializers.*`, `src/audio/HitPropertySetResource.*`
- The readers take the whole record as bytes. Streams handed to `Read(...)` must implement
  `cIStreamRemaining` (port-only; `PortMemoryIStream` does). The original asks the record-access view.
- Auto-detection: first dword `0x2bea4862` or `0xcbe750e0` -> binary reader (which only accepts
  `0x2bea4862`); anything else -> XML reader. XML parsing uses the port's `xml::Parse` (no expat).
- Binary: types handled are Uint32/Float32/Boolean/String; Sint32 and unknown types are skipped
  *without* consuming a payload, so a file containing them desynchronises (as in the binary).
  Writing an empty set or a Sint32 value fails.
- XML: root `<cGZPropertySetUint32>`; the reader's nesting counter starts at 0, so the closing root tag
  is processed like an entry (a document with no entries therefore fails).
- Legacy name-hashed XML (`Property{Uint32Uint32,Uint32Float32,Uint32String,StringString}`): key =
  `CRC24(Name)|0xff000000`; numeric values must start with a digit (negative numbers are rejected).
- Track resources: type `0x0b8d821a` reads/writes the comma-separated legacy text; reading records the
  resource's own instance in register `0xff6f9546` and rejects a sound id equal to that instance.
- `CreatePrivateCopy` also copies the source's reference count into the copy (binary behaviour).

---------------------------------------------------------------------------------------------------
## 9. Tooling invariants
- `scripts/ledger_class.py`, `ledger_rename.py`, `check_unique_names.py`: every recovered function has
  a class-unique descriptive name in Ghidra and in `notes/ledger.csv`.
- `scripts/run_tests.sh` must pass before every commit.

## 10. SC4 audio layer: sound manager (src/sc4audio/SoundManager.*)

`cSC4AudioSoundManager` (+ `tSound`) sits between the game and the sound system.

**Seams** (`SC4Audio::GetServices()` in `SC4AudioHost.h`, fill before `Init()`): `soundSystem`
(cIGZSndSys), `soundManager` (cIGZSndMgr), `messageServer`, `game` (audio options, simulation
paused, listener eye/target) and `exemplars` (tuning exemplar 6534284a/ca25875d/6a359c5c).
`SC4Audio::SetViewUtilities` must also provide `GetCameraEye/Target`. Register
`manager->AsMasterVolume()` with `SC4Audio::SetSoundManager` for the emanation models.

**Public API**: StartSound(soundId, instanceId=0 → generated), StopAllSounds, SetMasterVolume,
SetSoundPosition (ints, truncated), UpdateAllSounds; each posts a synchronous event to the sound
system which comes back through `DoEvent`.

**Behaviour to rely on**: same instance id restarts; a non-looping retrigger of one sound id within
50 ms is dropped; per-id (default 3) and total (default 10) limits apply only when that id is already
playing; looping sounds of one id become master/slaves (one voice, volume-weighted average
position, never played themselves); losing focus kills all voices and mutes, regaining restarts
them and bounces volumes for 50 ticks; loops farther than 4x min distance pause until the camera
returns (3D model only); flag 1 on start = pause with the simulation.

**Deviations**: no listener / zero weight → own position instead of NaN registers; no game
context → master volume stays 1024; Resume releases the track-player reference the original leaks;
loops that call out iterate over a referenced snapshot; a sound missing from the id map is dropped
from the list during StopAll (original could spin).

## 11. Sound system (cGZSnd family) — what the engine integration must provide

Everything below is recovered, ASan/UBSan-tested (`scripts/run_tests.sh`) and ledgered.

### 11.1 Service wiring (`src/audio/AudioServices.h`)
| Setter | Original lookup | Used by |
|---|---|---|
| `Audio::SetSoundSystem` | service `0x23cecba0` / iid `0x106077d2` | cGZSnd, cGZSndMgr, cGZSndPropertyCache, cGZSndDefaultEventHandler, cGZSndSysMac |
| `Audio::SetSoundManagerService` | service `0x1af85bfb` / iid `0x3af85bb7` | cGZSndDefaultEventHandler, cHitSegment*Creator |
| `Audio::SetPersistService` | service `0x056b906e` / iid `0x656b8efc` | data sources, property cache, creators, cGZSndMgr |
| `Audio::SetMessageServer` | service `0x04fa845b` / iid `0x652294c7` | cGZSndSys (event type `0xdb53383b`) |
| `Audio::SetUTalkCodec` | SND library `_utalkinit/_utalkdecode` | cGZSndSegmentDataSourceUTalk |
| `AudioPlatform::SetBackend` | AVAudioPlayer + SDL timers | CPCMPlayer, CQTRemotePlayer, cGZSnd fades |

Create in this order: `cGZSndSys::Create()` (Init), `cGZSndMgr::Create()` (Init), `cGZSndSysMac::Create()` (Init: registers the
renderer and creates the listener), then the creators / default event handler. Services are non-owning pointers;
the objects keep their own references.

### 11.2 Behaviour the game code relies on
* **Events**: `DoEvent*` dispatch synchronously to handlers (first consumer wins); `PostEvent*` queue and are drained by
  `Update` (every 20 ms from the callback timer, 26 ms budget) and only executed while a renderer is registered.
  Handlers/targets (un)registered from inside a callback are deferred until the loop ends.
* **Targets**: interval 20 ms ticks every update, anything else every >250 ms. `Panic`/`Shutdown` kill every target that is a sound.
* **Listener**: `cGZSndSys::SetPosition/Orientation/...` store the values and forward them to every renderer exposing
  `0x406077d2` / `0x5b30e7b3`. Platform listener stores positions remapped `(x,y,z) -> (-y, z, x)`.
  `cGZSnd3D::Update3DSound` distance model: full volume below the minimum distance, `max(min,1)/dist` above, split
  between ears by `dot(cross(up, front), dir)`; closer than 0.1 → `0.8125 * master volume` per ear (clamped to 1).
* **Master volume**: `cGZSndSys::SetMasterVolume(v)` ignores `v` (original bug) — scale volumes in the caller
  (cSC4AudioSoundManager already does).
* **cGZSnd**: volume `0..1024` scaled by the sound system master volume; `QueryInterface(1)` is *not* accepted (use
  `0x206077d2`); `IsPlaying()` of PCM sounds stays true after `Stop()`; completion notifications do not exist
  (`SetCompletionNotification` returns false — the SC4 sound manager's callback is therefore never invoked by cGZSnd; hit
  channels/track players do call it).
* **Data sources**: record lookup tries XA (`'XA'` header), UTalk (`UTM0`), then RIFF wav, per group registered with
  `AddSndDataSourceDef`. Locked reads of wav sources return zeroed memory (original: uninitialised).
* **Property cache**: global properties live in instance `0xcc3014ee`; the instance must be created with `AddInstance` first
  (otherwise Set/Get fail like the original). Instance cache `HasInstance/RemoveInstance` ignore the instance id.

### 11.3 Deviations from the binary (all deliberate)
| Where | Original | Port |
|---|---|---|
| cGZSnd::Load | double `delete[]` of the PCM buffer on open failure | single ownership |
| cGZSnd::FadeVolume | division by zero for durations < 20 ms | applies the target immediately |
| cGZSnd3D listener getters | never write their outputs | return false |
| cGZSndSys::PostEvent without queue | null deref | returns true, drops the event |
| cRZIniFile / cHitIniFile, cSC4AudioCOMDirector | not recovered | **blocked on the persistence layer** (see §12) |

## 12. Audio items intentionally deferred (and why)
| Item | Functions | Blocker |
|---|---|---|
| cHitIniFile + cRZIniFile, cSC4AudioCOMDirector | 26 + 22 + 8 | needs cIGZFile / persistence DB segments (`cGZDBSegmentPackedFile`, `SC4GetResourceAsFile`) and the COM class registry |
| cSC4AudioAmbienceManager/Layer, ~85 `cAmbience*Filter*`, cSC4AudioScapeManager, cSC4AudioEventHandler(+DisasterFire) | ~330 | need the world model: occupants, lots, city, lighting, view3d, simulator, vehicle manager |
| cSC4SoundDescription / cSC4SoundEffect | 22 | part of the effects framework (cSC4EffectDescription) |
| cSC4AudioAEMS* (engine sound), IDirectSound*_Mac | ~180 | wrappers over the EA SND library (`_SNDSYS_*`, `_SNDfx*`), counted with the C libs |
| cSC4AudioPreferencesWinProc | 25 | GUI window procedures (needs the UI toolkit) |

## 13. Audio demo (real game data)

`scripts/build_audio_demo.sh` builds `build/sc4audio`: it opens the game's `Sound.dat` through the
recovered persistence layer (`cGZDBSegmentPackedFile`), exposes it to the sound classes through
`Audio::SetPersistService`, creates data sources with `cGZSndSegmentDataSourceCreator` (EA XA ADPCM
and RIFF wav decoders as recovered), and plays the PCM through an `AudioPlatform::IBackend` built on
AVAudioPlayer.

    build/sc4audio --list 20            # sound records (1680 in 41 groups in the Steam install)
    build/sc4audio --sample 6           # play six sounds spread over the file
    build/sc4audio --wav out.wav 0a28b095   # decode one sound to a file
    build/sc4audio 3ee77108 bececfe8    # play specific instances

UTalk speech records need the EA SND library codec (not recovered, see section 12) and are skipped.
