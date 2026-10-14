# Hit / Fresh audio subsystem — architecture notes

Recovered from the vtables, RTTI and decompiled glue of Sim City 4 Deluxe Editionsub 1.2.1.
Raw decompiler dumps live in `work/audio/` (gitignored); recovered code is in `src/audio/`.

## Layers (bottom → top)
| Layer | Classes | Status |
|---|---|---|
| RZ framework streams / strings | `cRZIStream`, `cRZOStream`, `cRZString` | recovered (`src/rz`) |
| Register model | `nsHitReg` legacy→property table, `cIGZSndRegisterSet` | recovered |
| Emanation models | `cHitSem`, `cHitSem2dDefault`, `cHitSem3dDefault`, (`cSC4AudioEmanationModel2D/3D/3DEffects`) | Hit* recovered |
| Data | `cHitTrackDataBlock`, `cHitList`, `cHitResource<T>` | recovered |
| Resources / factory | `cHitResourceFactory`, `cHitPropertySetResource`, `cHitPropertySetTrackResource`, `cHitIniFile` | pending |
| Engine | `cHitMan` (manager), `cHitChannel`, `cHitControlGroup`, `cHitTrackPlayer` (bytecode interpreter), `cHitDebugInfo` | pending |
| Music ("Freshness") | `cFreshPlayer`, `cFreshDimensionPlayer`, `cFreshCell`, `cFreshCellPlayer`, `cFreshScore` | pending |
| GZ sound abstraction | `cGZSndSys`, `cGZSndMgr`, `cGZSnd`, `cGZSnd3D`, `cGZSnd3DListener`, data sources | pending |
| SC4 integration | `cSC4Audio*` (ambience, scape, AEMS events, emanation models, preferences) | pending |

## How the pieces connect
* **Registers.** A sound's state (volume, pan, frequency, 3D position, distances, priority, ...)
  lives in *registers* addressed by 32-bit property ids. `nsHitReg::GetPropertyIdFromLegacyRegisterId`
  maps the old small numbers. A **Sem** (emanation model) copies/derives registers from the logical
  sound to the playing channel (`UpdateVolPan`, `ConfigureChannel`).
* **Duck map.** `cHitMan::DuckMapSetSndobPri/DuckMapMaxPri` keeps the highest duck priority; a
  sound whose duck-priority register is below it plays at half volume (`/2048` instead of `/1024`).
* **Track players.** `cHitTrackPlayer` interprets "Hit" track bytecode stored in
  `cHitTrackDataBlock`s (entry points keyed by id). `cHitList` supplies no-repeat random choices
  (RNG = `cIHitMan::Random`).
* **Resources.** Persisted resource types handled by `cHitResourceFactory`: see `HitIds::kResType*`.
  `cHitResource<T>` adds a (type, group, instance) key to a payload.
* **COM registration.** `cHitManCOMDirector` and `cFreshnessCOMDirector` register the class ids in
  `src/audio/HitClassIds.h`. `PreAppInit` registers the resource factory for the four resource types
  and hands the segment data-source creator (`0x2b2eb869`) to the sound manager.

## Interface slot facts worth remembering
* `cIHitMan` sub-object sits at +0x28 inside `cHitMan`; `Random(int,int)` is its slot 26 (+0xd0),
  `GetRegisterVal(int)` slot 31 (+0xf8).
* `cIGZSnd` = vtable slots 3..36 (Init … SetSoundFile x2); `cIGZSnd3D` = 3..16; `cIGZSndRegisterSet` = 3..8.
* `cIGZOStream::SetUint32` is slot 9 (+0x48): the `cHitList` writer's `stream[+0x48](1)`.
