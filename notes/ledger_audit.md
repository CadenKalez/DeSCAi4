# Ledger provenance audit

Rows: **24787**. With a named verification: **23409**.
Without one: **1378** across **316** classes.

## Classes whose rows name no verification at all

These predate the evidence rule: their functions are decompiled and ledgered, but no test or oracle
is named. Repair means writing or pointing at a test, not editing the ledger.

| Class | Rows without evidence | Registered test compiling its source |
|---|---:|---|
| `cHitResource<cHitTrackPlayer>` | 56 | — |
| `cSC4DisasterManagerFire` | 47 | — |
| `cSC4DisasterManagerVolcano` | 31 | — |
| `cHitResource<cHitList>` | 20 | — |
| `cSC4DisasterManagerRiot` | 20 | — |
| `cSC4DisasterManagerEarthquake` | 15 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<0u, 31744u, 992u, 31u, 2, 5>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<0u, 31744u, 992u, 31u, 2, 5>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<0u, 16711680u, 65280u, 255u, 4, 8>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<0u, 16711680u, 65280u, 255u, 4, 8>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<0u, 31744u, 992u, 31u, 2, 5>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<0u, 31744u, 992u, 31u, 2, 5>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<0u, 16711680u, 65280u, 255u, 4, 8>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<0u, 16711680u, 65280u, 255u, 4, 8>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, nGZDrawSoftRender::RGBFormat<0u, 31744u, 992u, 31u, 2, 5>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, nGZDrawSoftRender::RGBFormat<0u, 31744u, 992u, 31u, 2, 5>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, nGZDrawSoftRender::RGBFormat<0u, 16711680u, 65280u, 255u, 4, 8>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, nGZDrawSoftRender::RGBFormat<0u, 16711680u, 65280u, 255u, 4, 8>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, nGZDrawSoftRender::RGBFormat<0u, 31744u, 992u, 31u, 2, 5>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, nGZDrawSoftRender::RGBFormat<0u, 31744u, 992u, 31u, 2, 5>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, nGZDrawSoftRender::RGBFormat<0u, 16711680u, 65280u, 255u, 4, 8>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, nGZDrawSoftRender::RGBFormat<0u, 16711680u, 65280u, 255u, 4, 8>, true>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, false>` | 13 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, true>` | 13 | — |
| `cS3DShapeFile` | 13 | — |
| `cRZCriticalSection` | 12 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<255u, 0u, 0u, 0u, 1, 0>, nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, false>` | 12 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<255u, 0u, 0u, 0u, 1, 0>, nGZDrawSoftRender::RGBFormat<61440u, 3840u, 240u, 15u, 2, 4>, true>` | 12 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<255u, 0u, 0u, 0u, 1, 0>, nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, false>` | 12 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<255u, 0u, 0u, 0u, 1, 0>, nGZDrawSoftRender::RGBFormat<32768u, 31744u, 992u, 31u, 2, 5>, true>` | 12 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<255u, 0u, 0u, 0u, 1, 0>, nGZDrawSoftRender::RGBFormat<0u, 31744u, 992u, 31u, 2, 5>, false>` | 12 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<255u, 0u, 0u, 0u, 1, 0>, nGZDrawSoftRender::RGBFormat<0u, 31744u, 992u, 31u, 2, 5>, true>` | 12 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<255u, 0u, 0u, 0u, 1, 0>, nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, false>` | 12 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<255u, 0u, 0u, 0u, 1, 0>, nGZDrawSoftRender::RGBFormat<0u, 63488u, 2016u, 31u, 2, 5>, true>` | 12 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<255u, 0u, 0u, 0u, 1, 0>, nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, false>` | 12 | — |
| `nGZDrawSoftRender::cBlitter<nGZDrawSoftRender::RGBFormat<255u, 0u, 0u, 0u, 1, 0>, nGZDrawSoftRender::RGBFormat<4278190080u, 16711680u, 65280u, 255u, 4, 8>, true>` | 12 | — |
| … 254 more classes | | |
