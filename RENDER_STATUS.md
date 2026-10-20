# RENDER_STATUS

| component | files | status | role |
|---|---|---|---|
| **Native Metal driver** `cMetalGDriver` | `src/render/gd/{IGZGDriver.h,GDriverBase.*,MetalGDriver.*}` | step 2a/2b done: clear, viewport/scissor, depth/alpha-test/blend/cull state, matrices, V3F_C4UB_T2F / 2T2F client-array draws (tris, strips, fans, quads, indexed tris), present, read-back | The renderer. Implements `cIGZGDriver` directly (RENDER_ARCH.md). Unsupported slot calls are counted (`UnsupportedCalls()`), never silently ignored. |
| Packed render state | `src/sc4/PackedRenderState.*` | recovered `PackRenderState`; `SC4RenderStateCache` = the diffing half of `SC4DrawContext::SetRenderState`; debug assertion `SC4_ASSERT_RENDER_STATE` (driver mirror vs state word) | Verified over all 34 literal render states in the game (`scripts/dump_render_states.py` → `scripts/tests/renderstates.inc`) and random sequences. |
| Diff harness | `scripts/run_render_diff.sh`, `scripts/tests/render_gd_diff_test.mm`, `scripts/tests/gdriver_on_gl.h` | working; 9 scenes, native == reference (max diff 0–1) | Native driver vs `GDriverOnGL`→MetalGL on the same call sequence; PNGs in `work/shots/gd_*.{native,ref,diff}.png`; analytic checks for what the reference can't do (scissored clear, quantisation). |
| **MetalGL** (GL-on-Metal) | `src/render/MetalGL.{h,mm}` | **frozen scaffolding / visual reference only** (runs headless: verified by the harness) | Not the renderer; not to be extended. Does not advertise `GL_APPLE_vertex_array_object/fence/element_array`, so the shim keeps to its device-level array mirrors. |
| Recovered D3D7→GL shim | `src/render/{D3DDevice7,DDSurface7,D3DVertexBuffer7,MacOpenGL,MacDisplay,MacPreferences,GLState,FastATIVertexProgram,GLApi,DX7*}.*` | recovered, unit-tested (`render_dx7`) | Fidelity reference for what the Mac build does beneath the driver; not on the final path. |
| Driver-level helpers | `nsTexelOp`, `nSGLDX7` index/vertex/format helpers, `nRZSimGL`, `cTextureStageData` | recovered earlier, oracle-verified | Reused by the native driver. |

## Observable quirks of the shipped driver now reproduced
* `ColorMask()` is a flush-only no-op (the colour write bit of the packed state is requested but has no effect).
* Clear flag bits are the driver's own: colour `0x4000`, depth `0x1000`, stencil `0x2000`; ClearColor/AlphaFunc quantise via
  round-half-even(f·256) with 256→255 (`0.5 → 128`, `1.0 → 255`).
* `SetViewport(x,y,w,h)` also enables the scissor; `SetViewport()` disables it; `GetViewport` returns `(x, y, x+w, y+h)`.
  A scissored `Clear` only touches the rectangle (GL coordinates, origin bottom-left).
* Depth is written only while the depth test is enabled. `Enable(CullFace)` = cull back faces with counter-clockwise front.
* `MatrixMode(0/1)` + `LoadMatrix` take D3D row-vector matrices unchanged (target 0 = view, 1 = projection; other targets ignored).

## Next (bring-up order)
1. **UI path**: texture table with the shipped handle/ID semantics (`GenTextures`, `CreateTexture`, `LoadTextureLevel`, `SetTexture`,
   `TexParameter`, `TexEnv`, `TexStage`), BC1/2/3 + 16-bit upload, `kTexture0` function constant, then `cGZDrawContextRenderToSimGL`
   scenes (textured quad lists, 2048-vertex chunks, alpha test 0.5, SRC_ALPHA blend) through the harness.
2. Backing-store service (`IBackingStore`) + `cSC43DRender` raw-GL functions recovered against it.
3. Terrain / vertex-buffer extension (persistent MTLBuffers), models, lighting-free fog, snapshots.

## Shipping-vs-reference rule (non-negotiable)
The shipping build renders 100% through native Metal: no OpenGL/CGL/AGL/D3D7/DirectDraw code or symbols, no GL-on-Metal or
D3D7-on-Metal layer, no generic GL state machine — Metal is driven from `cIGZGDriver` with SC4's own render state. The DX7 shim,
`MetalGL` and `GDriverOnGL` are **reference-build only** (tests/harness). The game's own direct GL calls (the `cSC43DRender`
backing store) must be ported to Metal driver services (`IBackingStore`), not emulated.
* Shipping sources are listed in `scripts/shipping_render_sources.txt`; nothing else may be added to a shipping target.
* **Gate:** `scripts/verify_shipping_render.sh` builds them, then runs `otool -L` and `nm` over the check binary (and `build/sc4`
  if built) and fails on any OpenGL link or GL/D3D7/DirectDraw/shim/MetalGL/GDriverOnGL symbol. It must pass before any
  rendering milestone is called done. Last run: pass (render_ship_check, build/sc4).

## Terrain view smoke test
`scripts/run_terrain_shot.sh` renders a procedural terrain through the recovered `cSTETerrainView3D` (src/sc4/cSTETerrainView3D*.cpp) into the
native Metal driver (headless) and writes `work/shots/terrain_shot.png`: height-coloured land, water surface with shoreline alpha, city-edge
cliffs, water box. Not the game boot path: no textures yet (the texture-less colour pass only), a demo camera (culling off), and the draw context
is a small adapter in `scripts/terrain_shot.mm` (`ITerrainDrawContext` -> `cIGZGDriver`). Booting the real game still needs the city component classes.
