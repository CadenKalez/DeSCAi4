# Graphics driver scope for the native Metal port

The Aspyr build wraps OpenGL in a fake DirectX 7 layer (nSGLDX7::*, CDirect*7, cSGLDX7*). A native Metal
port replaces that layer entirely, so only behaviour the game can observe is recovered 1:1.

## Recover (backend-independent, observable)
- nSGLDX7 free functions: quad/strip/fan -> triangle-list index generation, index width conversion,
  vertex conversion to V3F_C4UB_2T2F, texture format tables and converters.
- nRZSimGL: vertex format pack/stride/offset/extract, FindBitCounts, colour-key-to-alpha, texture tint.
- nsTexelOp pixel-format converters (done, bit-exact vs Ghidra: scripts/run_texel_oracle.sh).
- nSGLDX7::cTextureStageData (env mode / colour & alpha combiner semantics -> Metal shader selection).
- nSGLDX7::cGDriver state machine behind a backend interface (Metal implements the backend).
- cTextureManager / cPrimitiveManager / cVertexBuffer logic that is not DX7 COM plumbing.

## Skip (DX7 emulation, replaced by Metal)
- CDirectDraw7, CDirectDrawSurface7, CDirect3D7, CDirect3DDevice7, CDirect3DVertexBuffer7,
  cSGLDX7Direct3DDevice7Stub, cSGLDX7D3DX, DX7 COM calls inside the managers.
- nRZSimGL::ConvertTextureDXT*, nsTexelOp::Convert_DXT*: Metal samples BC1/BC2/BC3 natively on Apple silicon.

## Status (end of the first graphics pass)
Done, each verified bit-for-bit (or call-for-call) against the Ghidra output by an oracle script:
- nsTexelOp (scripts/run_texel_oracle.sh), nSGLDX7 index/vertex/format helpers (run_gdindex_oracle.sh),
  nRZSimGL vertex formats / colour key / tint (run_simgl_oracle.sh), nSGLDX7::cTextureStageData (run_texstage_oracle.sh).
- scripts/gen_block_oracle.py turns any set of Ghidra function bodies into a C oracle (`--table`, `--sub` for
  data blobs and lost call arguments); reuse it for further classes.

Deliberately not ported (replaced by the Metal backend, listed as `pending`/`recovered-partial` in the ledger):
- cGDriver (D3D7 device state, viewports, video modes, buffer regions): the Metal driver should implement the
  cIGZGDriver contract directly. Re-read the matching cGDriver function when a call's exact behaviour matters
  (defaults are visible in its constructor and Init).
- cTextureManager / cPrimitiveManager / cVertexBuffer: DX7 surface and vertex-buffer plumbing. The Metal path
  needs its own texture table and vertex ring; the index/vertex conversion they call is already recovered.
- CDirectDraw7, CDirectDrawSurface7, CDirect3D7, CDirect3DDevice7, CDirect3DVertexBuffer7, cSGLDX7*.
- DXT decoders (Metal samples BC1/BC2/BC3 natively).
Quirks preserved on purpose because they change output: nsTexelOp ARGB8->RGB8 is a 4-byte copy, ARGB4->ARGB8
packs A,R,G,B bytes, the 16-bit tinters advance rows by stride-2*width, DXT2345 tint reads its index word from
offset 4 and never swaps endpoints, min-filter mode 5 falls through into mode 6.
