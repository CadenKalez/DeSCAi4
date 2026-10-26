# City load: every object read to its exact end

All three tutorial cities (Making Money, Big City, Getting Started) load with no failing and no short object read.
Check: `SC4_DEBUG=1 SC4_LOADCITY=<city.sc4> sc4_game` prints a `LoadClassObjects:` line only for an object whose read failed, errored or
did not end at the object's size; the output has none. `SC4_DUMPFAIL=1` prints the first payload bytes of a bad object and `SC4_DUMPFULL=1`
writes the whole payload to `/tmp/payload_<class>.bin`; both are taken *before* the read, because a failed read puts the stream in error and
every later byte then reads as zero.

Method: the original `Read` functions were decompiled from the binary (Ghidra, `scripts/ghidra-mcp-headless.sh`), compared with the port's
version, and the payload was parsed independently where the two disagreed.

## What was wrong
| class | fix |
|---|---|
| Demand simulator, Residential, BuildingDevelopment, Fire / Police (protection base), Crime, Plumbing, PropDeveloper, Pollution, Flora, ZoneDeveloper, PropManager | the city file stores grids, lots, occupants and helper objects as references; the port read them through hosts that did not consume them. Each `Read` now has a city-file path (taken when the stream is a DB segment stream) that reads or consumes every reference; plain streams (the unit tests) keep the port's reduced record. |
| Weather | a water area list is a 32-bit count of four-integer rects; the "unused" field is 32 bits |
| Traffic simulator | `Read` was not recovered; now parsed in full for versions 0x1d and up (a 32-bit field the decompilation does not show sits before the scan iterator) |
| Traffic network map, Lot developer | `Read` / `Write` recovered |
| Watercraft manager | its inherited automata half (a real `cSC4AutomataManager` behind `tools/automata_base_handle`) and a neutral host are connected when the city creates it |
| Automata generators (831 objects) | the controller base record is consumed |
| Demand object | one byte (a flag) sits between the nine values and the two modifiers |
| Protection base | the walk iterator was skipped |
| Lot retaining wall | the lot reference is consumed |

## Still consumed, not stored
The traffic simulator and traffic network map keep none of their loaded state, and several simulators drop the object references they read
(the grids of the unrecovered population model, building lists, automata). The record is in step; modelling that state is the next work.
