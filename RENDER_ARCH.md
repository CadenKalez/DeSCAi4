# RENDER_ARCH — where the native Metal renderer plugs in

Status: **survey only — no renderer code has been written against this document yet.** Awaiting review.

## 0. Correction of course

`AGENT_RULES.md` / `.claudecode` / `notes/graphics_scope.md` already said: *native Metal, no OpenGL, skip the DX7
emulation.* This session went the other way: it recovered the D3D7-over-OpenGL shim (`src/render/D3DDevice7.*`,
`DDSurface7.*`, `D3DVertexBuffer7.*`, `MacOpenGL.*`, `MacDisplay.*`, `GLState.*`, behind the `IGLApi` seam; commit
"Recover the DX7-over-OpenGL shim…") and then began `MetalGL` (GL-on-Metal). Both are now **reference material**:
the shim recovery documents exactly what the Mac build does *beneath* the driver interface (useful for fidelity), and
`MetalGL` is bring-up scaffolding. Neither is the target. Details in `RENDER_STATUS.md`.

## 1. The layer stack (from the binary)

```
 game systems                                   cSC43DRender (130 fns), cSTETerrainView3D, particle effects,
   │  (draw calls, SC4 render state)            cS3DModelInstance…, UI: cGZDrawContextRenderToSimGL, cGZBufferSimGLCache
   ▼
 SC4DrawContext  (52 fns, 0x100771d00…)   ← SC4's own render-state layer; owns a cIGZGDriver* at +0x40
   │   cSC4PackedRenderState {value, mask}  (SetRenderState → diffs against its cached word at +0xd8)
   ▼
 cIGZGDriver  ◄── THE ENGINE-LEVEL GRAPHICS DRIVER INTERFACE (GZCOM driver, GL-1.x-shaped, 83 vtable slots)
   + cIGZGBufferRegionExtension, cIGZGSnapshotExtension, cIGZGDriverLightingExtension,
     cIGZGDriverVertexBufferExtension          (all obtained by QueryInterface on the same object)
   ▼
 nSGLDX7::cGDriver  (+ cTextureManager, cPrimitiveManager, cVertexBuffer, cTextureStageData, cSGLDX7D3DX)
   │   "SGL driver, DX7 flavour": translates the GL-shaped calls into IDirect3DDevice7 / IDirectDraw7 calls
   ▼
 CDirect3DDevice7 / CDirectDrawSurface7 / CDirect3DVertexBuffer7 / CDirectDraw7   (Aspyr's fake DX7)
   ▼
 OpenGL (MacOpenGL, MacDisplay, SDL window + CGL context)
```

Driver selection: `cGZGraphicSystem` (108 fns; `cGZGraphicSystemW32` 35) loads drivers by GZCOM class id
(`LoadAllDrivers`, `CallbackDriverLoad`, `GetGDriverByID`, `SetGDriver`); `GZCOM_CreatecSGLDriverDX7` (0x100603657)
constructs `nSGLDX7::cGDriver`; `cSC4GDriversCOMDirector` registers the SC4-side drivers; `cSC43DRender::SetGDriver` /
`StaticSetGDriver` hand the pointer to the 3D renderer. **A native `cMetalGDriver` is a drop-in replacement at exactly
this point** — everything above it is untouched, everything below it disappears.

## 2. The interface to implement: `cIGZGDriver` (+ extensions)

Evidence: `nSGLDX7::cGDriver` vtable @ 0x100ad53e0, five sub-objects; `cGDriver::QueryInterface` (0x10060401e) answers
iid `1`/`0xa4554849` → cIGZGDriver (+0x18), `0x87e2b87d` → lighting ext (+0x10), `0xe69bfe2a` → snapshot ext (+8),
`0x669565fe` → buffer-region ext (this), `0x09cd86f9` → vertex-buffer ext (the `cPrimitiveManager`, +0x38).

### 2.1 cIGZGDriver core (vtable group 3, 83 slots; offsets are 8 × slot)
| slots | methods | Metal meaning |
|---|---|---|
| 0–2 | QueryInterface / AddRef / Release | — |
| 3–10 | DrawArrays, DrawElements, InterleavedArrays | immediate draws from client memory (UI, small batches) |
| 6–10 | MakeVertexFormat ×2, VertexFormatStride / ElementOffset / NumElements | pure functions — **already recovered** in `nRZSimGL` / `nSGLDX7` helpers |
| 11–14 | Clear, ClearColor, ClearDepth, ClearStencil | load actions |
| 15–19 | ColorMask, DepthFunc, DepthMask, StencilFunc/Mask/Op | pipeline write-mask / depth-stencil state |
| 21–23 | BlendFunc, AlphaFunc, ShadeModel | pipeline blend state / fragment constant |
| 24–29 | BindTexture, TexImage2D, PixelStore, TexEnv ×2, TexParameter | legacy texture path |
| 30–34 | Fog ×2, ColorMultiplier, AlphaMultiplier, EnableVertexColors | fragment constants |
| 35–39 | GenTextures, DeleteTextures, IsTexture, PrioritizeTextures, AreTexturesResident | texture table |
| 40–50 | MatrixMode, LoadMatrix, LoadIdentity, Flush, Enable/Disable/IsEnabled(eGDDriverState), GetBoolean/Integer/Float, GetError | state |
| 51–57 | TexStage, TexStageCoord, TexStageMatrix, TexStageCombine ×4 | per-stage combiner / texgen / matrix |
| 58–62 | SetTexture, GetTexture, CreateTexture, LoadTextureLevel, SetCombiner(cGDCombiner) | modern texture path (hint flags) |
| 63–66 | CountVideoModes, GetVideoModeInfo ×2, SetVideoMode | window / drawable |
| 67–73 | PolygonOffset, BitBlt, StretchBlt, BitBlt/StretchBlt Alpha / AlphaModulate | 2D blits |
| 74–78 | SetViewport ×2, GetViewport, GetDriverInfo, GetGZCLSID | — |
| 79–82 | Init, Shutdown, IsDeviceReady, Punt | lifecycle |
Extensions: buffer regions (New/Delete/Read/DrawBufferRegion, IsBufferRegion, CanDoPartialRegionWrites/OffsetReads),
snapshot (`CopyColorBuffer`), lighting (EnableLighting, EnableLight, LightModelAmbient, LightColor ×2, LightPosition,
LightDirection, MaterialColor ×2), vertex-buffer (`GetVertexBufferName`, `VertexBufferType`, `MaxVertices`,
`GetVertices`, `ContinueVertices`, `ReleaseVertices`, `DrawPrims`, `DrawPrimsIndexed`, `Reset`).

### 2.2 SC4's render state: `cSC4PackedRenderState` → `SC4DrawContext::SetRenderState` (0x100773906)
A `{value, mask}` pair of 32-bit words (`PackRenderState`, 0x100771d00, 14 parameters; `-1` = "don't care" clears the
mask bits). `SetRenderState` XORs against the context's cached word (+0xd8) and issues only the changed driver calls:

| bits | field | driver call |
|---|---|---|
| 0 | colour write | `ColorMask` (slot 15) |
| 1 | depth write | `DepthMask` (17) |
| 2 | depth test | `Enable/Disable(state 1)` (44/45) |
| 3 | alpha test | `Enable/Disable(state 0)` (skipped if device cap +0x238 "force no alpha test") |
| 4 | blend | `Enable/Disable(state 4)` (skipped if cap +0x1f8 "force no translucency") |
| 5 | texture stage 0 on | `TexStage(0)`; `Enable/Disable(state 5)` |
| 6 | cull face | `Enable/Disable(state 3)` |
| 7–9 | depth func | `DepthFunc` (16) |
| 10–12 + 21–24 | alpha func + ref (= n × 0.0666667) | `AlphaFunc` (22) |
| 13–16 / 17–20 | blend src / dst | `BlendFunc` (21) |
| 25–27 | TexEnv mode, stage 0 | `TexEnv(…)` (27) |
| 28 / 29–31 | stage 1 on / TexEnv mode (only if cap +0x2f8 "second stage") | `TexStage(1)`… |

That is the whole fixed-function **pipeline-state key SC4 draws with** (≤ 2^30 combinations, a few dozen in practice).

## 3. Design for the native Metal driver (to implement after review)
* **Pipelines keyed by SC4's render state, using Metal function constants, not runtime branches.** Function constants:
  alpha-test on/func, texture stage 0/1 enabled, TexEnv/combiner mode per stage, fog on, vertex-colour on, lighting
  path, vertex format. Blend factors, write mask and depth/stencil go in the pipeline / depth-stencil descriptors.
  Cache: `key = (packedState, combiner ids, vertex format) → MTLRenderPipelineState`, built lazily, pre-warmed from the
  states `SC4DrawContext::Init/InitDrawStates` create (they are enumerable: ~40 `PackRenderState` call sites).
* **Static geometry in persistent `MTLBuffer`s.** The vertex-buffer extension (`GetVertices/ContinueVertices/
  ReleaseVertices/DrawPrims(Indexed)`) already hands out writable memory and a name: map it to a persistent shared
  buffer per vertex-format name; terrain (`cSTEVertBufExtn`, fixed 32-byte `V3F_C4UB_2T2F`) and particles use it with
  no re-gathering. Only `DrawArrays/DrawElements` from *client* pointers (UI, small dynamic batches) go through a
  ring buffer, and only because the pointer's lifetime ends at the call.
* Per-draw constants (matrices, fog, colour multipliers, combiner constants) via `setVertexBytes/setFragmentBytes`;
  texture table = `GenTextures/CreateTexture/LoadTextureLevel` with hint flags → `MTLTexture` (BC1/2/3 native, 16-bit
  formats converted once at load by the already-recovered `nsTexelOp` converters).
* One command buffer per frame; passes split only by `Clear` with load-action changes, buffer-region/snapshot reads.
* Math with `<simd/simd.h>` (NEON / SSE) — no assembly (`.claudecode` rule 2/3).

## 4. D3D7 / DirectDraw call sites that bypass the driver interface
Method: whole-program xrefs (Ghidra) to the shim's entry points, plus a signature scan of the symbol table for
`IDirect3D*/IDirectDraw*/_D3D*/_DD*` outside the driver and shim classes (only audio's `_D3DVECTOR` matched).
**No game code outside the `nSGLDX7` driver calls IDirect3DDevice7 / IDirectDraw7 / surfaces directly**: the DDraw
object is created only by `cSGLDX7D3DX::InitDirectDraw` (via `CreateSGLD3DXContext` ← `cGDriver::SetupD3DX`, and
`CreateSGLD3DXContextInfoOnly` ← `cGDriver::InitViewport`), and the device/surfaces only by `MacOpenGL::InitializeOpenGL`
(← `cSGLDX7D3DX::ReinitDevice`). The bypasses are all *capability leaks* of the Mac shim into game code:

| call site | what it reads | why it matters for Metal |
|---|---|---|
| `cSTEVertBufExtn::LoadVertRowIntoBuf` (0x10075170e), `FlushBuffer` (0x100751798) | `MacOpenGL::IsFastATICard()`, `IsVertexArraySupported()`; vertex stride 0x20 vs 0x38; `FastATIVertexCopiers::pfCopyFunc` | with `IsFastATICard() == false` (hard-wired in the binary) terrain vertices are 32 bytes and copied with `memmove` |
| `cSTETerrainView3D::DrawTerrainMeshSubsetWithVertBufExtensionInDrawFrustum` (0x100749ace), `…InViewFrustum` (0x10074a72c) | same two queries | terrain draw path |
| `cSC4ParticlesEffect::RenderActiveParticlesVB` (0x1005f6548), `cSC4GeometrySourcedParticlesEffect::RenderActiveParticlesVB` (0x1005eb958) | same two queries around `GetVertices/ContinueVertices/ReleaseVertices` | particle vertex path |
| `nSGLDX7::cVertexBuffer::cVertexBuffer`, `cPrimitiveManager::QueueVersionA`, `DrawPrimsIndexed`, `CopyToVertexBuffer_V3F_C4UB_2T2F` | same queries + `CDirect3DVertexBuffer7::GetFVFSize` | inside the driver (will be replaced) |
Resolution: the Metal driver reports `IsFastATICard=false`, `IsVertexArraySupported=false` equivalents by construction —
i.e. the 32-byte non-VAR layout — and these callers are left unmodified. (The recovered `MacOpenGL` statics are only
needed while the old stack is alive.) `cSC4GDriversCOMDirector`/`cGZGraphicSystem` also read video-memory and video-mode
data via `MacDisplay::GetVideoMemoryInfo`/`GetNumModes`/`GetNthMode` (through `cGDriver::CountVideoModes`,
`VideoMemory`) — a Metal driver supplies those directly.

## 5. Known shim quirks and their disposition (fidelity rules: preserve what the game can observe)
Observable through the driver interface → **preserve**:
1. **No hardware lights.** `SetLight`, `LightEnable`, `GetLight` are empty; only material ambient/diffuse, model ambient
   and the vertex-colour source are honoured. Lit output on the Mac is vertex colour × texture. Do not implement lights.
2. **Texture-stage translation**: COLOROP/ALPHAOP map to the GL combiner as in `SetTextureStageState` (MODULATE2X/4X
   scale, ADDSIGNED(2X), BLENDTEXTUREALPHA's extra source/operand, DOTPRODUCT3), args 0..3 → primary/previous/texture/
   constant; `COLOROP == DISABLE` disables the unit; texture transform flag 0 resets to identity; `TEXCOORDINDEX` with
   the camera-space bit (0x20000) switches the unit to 3-float position coordinates multiplied by the view matrix.
3. **Mip/filter mapping** (`CTextureStage::SetTexture` vs `SetTextureStageState` differ for unknown mip-filter values),
   alpha-test ref = n/255, material-source interplay (diffuse/ambient `ColorMaterial` chosen from both sources).
4. **Blt/CopyPixels behaviour**: the benchmark that picks the depth-copy method runs only if `CreateScreenContext`
   reports a fast flag, which it never does; the `blt-method` preference (default −1) then forces the DrawPixels path.
5. **Vertex layout**: FVF offsets ignore the normal; both texcoord sets are always filled in; XYZRHW and XYZ-only handled;
   SC4 only uses `V3F_C4UB_2T2F` (0xb) so none of this changes pixels, but vertex-extension strides must stay 32.
6. **Clear** forces colour mask on and dither off, depth mask/func ALWAYS for the depth clear (state restored by
   push/pop attrib); **Draw*Primitive** without a vertex buffer, strided variants, state blocks, clip planes,
   `GetTexture/GetTransform` are no-ops/zero — the driver must (and does) use only the VB paths.
Pure shim artefacts (**do not carry over**): unchecked table indices (blend/compare/primitive lookups with unset state),
`GetPrivateData` on an empty slot, the null-group dereference class of bug, depth-range squeeze from loading D3D
projection matrices into GL (Metal maps clip z explicitly; keep depth *ordering*, not the [0.5,1] squeeze), the
`vertex-array` APPLE extension paths (never enabled), FastATI programs (dead code).

## 6. Proposed work order (nothing started)
1. Review this document; confirm the interface boundary (`cIGZGDriver` + 4 extensions, driven by `SC4DrawContext`).
2. Recover/verify the *driver-level* behaviours that the shim hides (needs the cGDriver bodies, not the shim): state
   defaults in `cGDriver::Init`/`SetDefaultD3DDeviceState`, `Enable/Disable(eGDDriverState)` enum values, `Fog`,
   `ColorMultiplier/AlphaMultiplier`, `SetCombiner`, `TexStageCoord/Matrix`, the lighting extension (what it does when
   the device has no lights), buffer regions, `BitBlt*` family, texture hint flags.
3. Define `IMetalFrame`-free `cMetalGDriver` skeleton with the state-key and function-constant plan; test headlessly
   (offscreen render + readback) against oracle images produced from the recovered driver semantics.
4. Terrain + models first (the vertex-buffer extension), then UI (`cGZDrawContextRenderToSimGL`), then buffer regions/
   snapshots.

## 7. Open questions for review
* Confirm: should `cGDriver`'s own bookkeeping (texture table, hint flags, video-mode enumeration, buffer-region ids)
  be recovered 1:1 (it is engine-visible) while its D3D7 calls are replaced? (Proposed: yes.)
* Window/drawable ownership: the existing host (`src/app/main.mm`) owns a CAMetalLayer; `SetVideoMode` should adopt it.
