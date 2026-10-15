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
