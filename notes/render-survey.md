# Metal render survey log

Host: `scripts/build_app.sh` -> `build/sc4` (arm64, Metal only, runtime-compiled MSL).
Screenshots: `build/sc4 --screenshot work/shots/<name>.png [--size WxH --yaw d --pitch d --zoom z] [city.sc4]`.

## Data format facts (verified against Timbuktu/City - Big City Tutorial.sc4)
- DBPF 1.0, 20-byte index entries; DIR record (0xe86b1eef) lists uncompressed sizes of QFS entries.
- Terrain = type 0xa9dd6ff4 / group 0xe98f9525 / instance 1: u16 version(2) + 129*129 float32 metres.

## Findings
| # | Shot | Finding | Status |
|---|------|---------|--------|
| 1 | 001 | Auto-fit left terrain at ~55% of viewport (radius constant too loose) | fixed (002) |
| 2 | 003 | Yaw rotation, sea level (250 m) flattening, shore shading render correctly | ok |

## Memory notes
- Terrain costs 4 B/sample in one R32Float texture (66 KB for 129x129); no CPU mesh/index buffers,
  CPU height array is released after upload. Vertex stage derives normals by central differences.
