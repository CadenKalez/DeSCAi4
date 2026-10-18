# RENDER_ARCH — where the native Metal renderer plugs in

Status: survey **approved** (decision on §7: recover `cGDriver`'s bookkeeping, preserving *observable* behaviour 1:1 — texture handles/ids, hint flags, video-mode list contents and order, buffer-region ids, return/error values, edge cases — with internals free to be Metal-natural). §8 below answers the five follow-up confirmations; implementation (step 2) is tracked in RENDER_STATUS.md.

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

---

# 8. Follow-up confirmations (before step 2)

## 8.1 Buffer regions and persistence of framebuffer contents
**Evidence.** `cSC43DRender::StaticSetGDriver` (0x1007614c6) queries the driver for the buffer-region extension (iid
0x669565fe) and the snapshot extension (0xe69bfe2a). The buffer-region pointer is then used **only** for slot 9
(`CanDoPartialRegionWrites()`, in `Init` 0x10076002a and `SetGDriver` 0x10076143c; the driver returns `true`, as do
`BufferRegionEnabled()` and `CanDoOffsetReads()`). A byte scan of the whole binary for the extension iids finds no other
query site: **`NewBufferRegion / ReadBufferRegion / DrawBufferRegion / IsBufferRegion` have no callers in game code.**
The snapshot extension (`CopyColorBuffer`) is used by `SC4TakeSnapshot`, `cSC4WinSnapshotTaker::TakeSnapshot`,
`cSC4WinAnimationRecorder::Init` and `cSC43DRender::TakeSnapshot`.

**What the city view does instead (bypass #1).** `cSC43DRender` keeps a *backing store* as a raw OpenGL framebuffer
object (`CreateBackingStore` 0x1007699ee: FBO + RGBA8 colour renderbuffer + 24-bit depth renderbuffer, sized to the
viewport; `sMacBackingStoreFBO` @0x100b90ed0) and moves pixels with `glBlitFramebufferEXT(mask = COLOR|DEPTH, NEAREST)`:
`SaveBackingStore(x,y,w,h)` (0x100764aec; screen → store), `RestoreBackingStore(...)` (0x100769b1c; store → screen),
`DoTranslatedViewUpdate(dx,dy)` (0x100763d10; scroll the store), plus `DeleteFramebuffersEXT` in `StaticSetGDriver`/
`StaticShutdown`. `Draw()` (0x1007659f6) is built around it: with the store valid it **restores the static scene from the
store, draws only the dynamic layer on top, and processes the dirty-rectangle queue** (`AddDirtyRectangle`,
`ProcessDirtyRectangleQueue`, `DoPartialViewUpdate`, `AttemptTranslatedViewUpdate`) by redrawing sub-rectangles into the
live frame and re-saving them. So yes: the city view relies on **persistent colour *and depth* between operations**,
and on blits whose rectangles are in GL (bottom-left) coordinates (`y' = height - (y + h)`), clamped against the camera
shake offsets at `this+0x398/0x39c`.

**Design.** The Metal driver owns two persistent target pairs and never loses them implicitly:
1. *frame target* — `BGRA8` colour + `Depth32Float`, load action `Load`/`Clear` only on explicit `Clear()`; presented by a
   blit to the drawable (`Present`, once per `cGZGraphicSystem::Draw`);
2. *backing store* — the same pair, created by the backing-store service when the driver is set (viewport-sized), with
   `Save/Restore/Translate` implemented as `MTLBlitCommandEncoder` copies (colour and depth, flipped rectangles).
`cSC43DRender`'s five raw-GL functions are recovered against this service (`IBackingStore`), not against GL: that is the
one place game code must be changed, and it is localised (`CreateBackingStore`, `SaveBackingStore` ×2,
`RestoreBackingStore` ×2, `DoTranslatedViewUpdate`, `StaticSetGDriver/StaticShutdown` cleanup). Buffer-region calls stay
inert (`BufferRegionEnabled=true`, `Can…=true`, `NewBufferRegion` returns the original failure value once recovered).
Dirty-rect redraws need nothing special beyond load-action `Load` and scissored draws (`SetViewport(x,y,w,h)` also sets
the scissor rect — see 0x100607176: `glViewport + glScissor + Enable(SCISSOR_TEST)`; the no-argument form disables it).

## 8.2 Pipeline key from the slot calls, and agreement with the packed state word
The driver mirrors exactly the state that `SC4DrawContext::SetRenderState` drives, field by field:

| packed bit(s) | driver slot call (group-3 slot) | driver-side field | key part |
|---|---|---|---|
| 0 | `ColorMask(b)` (15) | `colorMask` | pipeline `writeMask` |
| 1 | `DepthMask(b)` (17) | `depthWrite` | depth-stencil state |
| 2 | `Enable/Disable(1)` (44/45) | `depthTest` | depth-stencil state |
| 3 | `Enable/Disable(0)` | `alphaTest` | function constant `kAlphaTest` |
| 4 | `Enable/Disable(4)` | `blend` | pipeline `blendEnabled` |
| 5 | `TexStage(0)`+`Enable/Disable(5)` | `stage[0].enabled` | function constant `kTex0` |
| 6 | `Enable/Disable(3)` | `cull` | encoder cull mode |
| 7–9 | `DepthFunc(f)` (16) | `depthFunc` | depth-stencil state |
| 10–12, 21–24 | `AlphaFunc(f, n·1/15)` (22) | `alphaFunc`, `alphaRef` | function constants `kAlphaFunc`, ref in constants |
| 13–16 / 17–20 | `BlendFunc(src,dst)` (21) | `blendSrc/Dst` | pipeline blend factors |
| 25–27 | `TexEnv(0,0,mode)` (27) | `stage[0].envMode` | function constant `kTexEnv0` |
| 28 / 29–31 | stage-1 on / `TexEnv` mode (only when the device reports the second stage) | `stage[1]…` | `kTex1`, `kTexEnv1` |

`PipelineKey` is *derived from the driver's mirror*, never from the packed word. The packed word is recovered
(`cSC4PackedRenderState::PackRenderState`, `SC4DrawContext::SetRenderState`) and a **debug assert**
(`GDriverState::AssertMatches(packed)` → `SC4_ASSERT_RENDER_STATE`) is run after every `SetRenderState` in the harness
and in debug builds of the engine: it decodes the 30 field bits of `value` (honouring `mask`) and compares each against the
mirror; a mismatch aborts with the field name. The harness feeds it all catalogued states (8.3) plus random ones.

## 8.3 Only the combinations the survey found
The render states SC4 can request are all created through `PackRenderState`; the survey (call sites in 33 functions,
60 constant argument tuples) gives the closed set below. Parameter order is (colorMask, depthMask, depthTest, alphaTest,
blend, texStages{0,1,2→also stage 1}, cull, depthFunc, blendSrc, blendDst, alphaFunc, alphaRef, texEnv0, texEnv1).

* Distinct `depthFunc` values: 1, 2, 3, 4, 7, −1.  `blendSrc`: 0, 1, 4, 8, −1 ·  `blendDst`: 1, 2, 5, −1  (the common
  pair is SRC_ALPHA / INV_SRC_ALPHA = 4/5; others are the flash and highlight effects).
* `alphaFunc`: 1, 4 or −1 with refs 0.0, 0.1, 0.5 (n/15 quantised to 0, 2/15, 8/15).  `texEnv0`: 1 or −1; `texEnv1`: −1.
* Dynamic (computed) states: `SC4DrawContext::SetRenderState(cS3DMaterial)`, `cSC4ParticlesEffect::SetDrawMode`,
  `SC4PathInfoView`/`SC4MarkedCellView`/`cSC4NetworkLayoutView`/`cSC4NetworkPreview` (flags from properties), and the
  UI context's own `HWUpdateState` (calls `Enable/Disable(0,1,4,5)`, `AlphaFunc(GREATER,0.5)`, `BlendFunc(4,5)`,
  `MatrixMode(1)`, `LoadIdentity` directly).
The pipeline cache is therefore keyed on these field values only; any other combination raises `Unsupported` (counted and
logged, never silently emulated). The full catalogue (`scripts/dump_render_states.py` output) is checked into
`work/renderstates.txt` by the harness and the pre-warm list is generated from it.

Driver-state enum values (from the lookup tables at 0x100a26820..0x100a268e0): `eGDPrimType` 0 TRIANGLES, 1 TRI_STRIP,
2 TRI_FAN, 3 POINTS, 4 LINES, 5 LINE_STRIP, 6 QUADS, 7 QUAD_STRIP (QUADS/QUAD_STRIP/ strips/fans are index-expanded;
the expanders are recovered). `eGDTestFunc` 0..7 = NEVER, LESS, EQUAL, LEQUAL, GREATER, NOTEQUAL, GEQUAL, ALWAYS.
`eGDBlend` 0..10 = ZERO, ONE, SRC_COLOR, INV_SRC_COLOR, SRC_ALPHA, INV_SRC_ALPHA, DST_ALPHA, INV_DST_ALPHA, DST_COLOR,
INV_DST_COLOR, SRC_ALPHA_SAT. `eGDDriverState` 0 alpha test, 1 depth test, 2 stencil, 3 cull, 4 blend, 5 texturing of the
current stage, 6 fog (only if the device allows), 7 no-op.

## 8.4 Capability queries
`MacOpenGL::IsFastATICard()` is `false` and `IsVertexArraySupported()` is `false` in the Metal build, as constants (the
real functions read driver globals; the callers listed in §4 are compiled against `render/GDCaps.h`, which defines
`constexpr bool IsFastATICard() { return false; }` and `IsVertexArraySupported()` likewise). Vertex stride for the vertex-
buffer extension is therefore 32 bytes (`V3F_C4UB_2T2F`, format 0xb).

## 8.5 Visual reference and the diff harness
* **Comparison renderer: MetalGL** — verified running headless (`/tmp` smoke test: clear + vertex-coloured triangle read
  back correctly, 1 draw, 1 pipeline). It is driven *not* through the D3D7 shim but through a thin **`GDriverOnGL`
  adapter**: `cIGZGDriver` is GL-shaped, so each slot call maps to one or two `IGLApi` calls. That gives an independent
  implementation of the same driver semantics (different code, different shader, different state handling), so a disagreement
  points at one of the two; fidelity quirks of the old D3D7 layer are covered separately by targeted unit tests rather than
  by this reference.
* **Harness** (`scripts/render_diff.sh`, `tests/render_diff_test.mm`): a *scene* is a list of driver calls (a tiny text
  format or C++ lambdas). The harness runs it on both back ends at the same size, reads back BGRA, writes
  `work/shots/<scene>.{native,ref,diff}.png`, and reports max channel difference, count of pixels differing by > 2, and a
  pass/fail against per-scene tolerances (exact for flat colour/clear, ≤ 2 for textured/blended). It must pass on the
  **clear** and **solid 2D quad** scenes before any real feature lands.
* A second, manual reference is the original game itself under Rosetta (not automated; used only for qualitative checks).
