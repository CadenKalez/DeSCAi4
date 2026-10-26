# City load: object reads that do not finish (Making Money Tutorial)

Run `SC4_DEBUG=1 SC4_LOADCITY=<city.sc4> sc4_game` (scripts as in the session notes). `SC4_DUMPFAIL=1` prints the first 96 payload bytes of every
failing object, `SC4_DUMPFULL=1` writes the whole payload to `/tmp/payload_<class>.bin`. Both are taken before the read, because a failed read puts
the stream in error and every later byte reads as zero.

## Fixed
| class | name | cause |
|---|---|---|
| 2990bff2 | DemandSimulator | the file stores each demand as an object reference (id + class 0x8990c12d), not inline; the reads are loaded with `loadNow` and copied (plain streams in the unit tests stay inline) |
| 2990c1bc | WeatherSimulator | a water area is 3 integers (12 bytes), and the "unused" field is 32 bits, not 64 |

## Open (11)
| class | name | consumed | what the payload shows | what is missing |
|---|---|---|---|---|
| 6990c1aa | TrafficSimulator | 12 of 272272 | version 0x28 then references | `Read` is not recovered (returns false) |
| 6a0f82b2 | TrafficNetworkMap | 12 of 338821 | version 7, grid data | `Read` is not recovered (TRAFFICNETWORKMAP-3) |
| a990bfe0 | LotDeveloper | 12 of 2748 | version 1, references | `Read` is not recovered (returns false) |
| a98f9d28 | WatercraftManager | 14 of 52 | version 5 | no `IWatercraftManagerBase` / host adapter (base_ and host_ are null) |
| 8990c09a | CrimeSimulator | 137 of 137, error | refs, iterator, events | `ICrimeHost::ReadLot` is a stub (the lot references are not resolved) |
| 2990c05a / 6990c04f | Fire / Police | 32 of 120 / 176 | version 2 / 7, flags, floats, refs | stub service hosts (lots, buildings) |
| 4990c013 | ResidentialSimulator | 97 of 480 | version 7, then lists of (id, class) references | the recovered `Read` expects a population and flag first; the first 5 bytes after the version are a count |
| 8990be7a | BuildingDevelopmentSimulator | 878 of 881 | version 4, two references, 12 (key, reference) entries | the recovered `Read` has no reference fields |
| 0990c075 | PlumbingSimulator | 16550 of 17035 | version 4, two grid references | 485 bytes unread after an error |
| 89c48f47 | PropDeveloper | 29 of 6835 | version 5, flags, counts | needs the original `Read` |

The four unrecovered `Read` functions and the host adapters need the original decompilation (Ghidra) or per-class format analysis like the two fixes above.
