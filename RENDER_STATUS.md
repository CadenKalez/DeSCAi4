# RENDER_STATUS

| component | files | status | role |
|---|---|---|---|
| **MetalGL** (GL-on-Metal) | `src/render/MetalGL.{h,mm}` | compile-checked only, never run; **frozen** | **Temporary bring-up scaffolding and visual reference only.** Not the renderer, not to be extended. Implements `IGLApi` so the recovered D3D7 shim can be exercised on a Metal device. Does **not** advertise `GL_APPLE_vertex_array_object` / `GL_APPLE_fence` / `GL_APPLE_element_array` (checked: `GetString(GL_EXTENSIONS)` lists only `SGIS_texture_lod, ARB_window_pos, ARB_texture_compression, EXT_texture_compression_s3tc, ARB_multitexture`), so `MacOpenGL::IsVertexArraySupported()` stays false and the shim uses its device-level array mirrors (`UpdateNonVarPtrs`); `BindVertexArrayAPPLE` being a stub therefore cannot desync the per-buffer client-state cache. |
| Recovered D3D7→GL shim | `src/render/{D3DDevice7,DDSurface7,D3DVertexBuffer7,MacOpenGL,MacDisplay,MacPreferences,GLState,FastATIVertexProgram,GLApi,DX7*}.*` | recovered, unit-tested (`render_dx7`) | Reference for fidelity (what the Mac build does beneath the driver). Not on the path of the final renderer. |
| Driver-level helpers | `nsTexelOp`, `nSGLDX7` index/vertex/format helpers, `nRZSimGL`, `cTextureStageData` | recovered earlier, oracle-verified | Reused by the native Metal driver. |
| **Native Metal renderer** | — | **not started** | Target: `cMetalGDriver` implementing `cIGZGDriver` + extensions; design in `RENDER_ARCH.md`, pending review. |
| Terrain/building viewer | `src/render/MetalRenderer.mm`, `src/app` | existing prototype | Independent of the above. |
