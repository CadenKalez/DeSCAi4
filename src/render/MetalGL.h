// *** BRING-UP SCAFFOLDING / VISUAL REFERENCE ONLY — NOT THE RENDERER. See RENDER_ARCH.md and RENDER_STATUS.md. ***
// This is OpenGL-on-Metal (it sits beneath the recovered D3D7->GL shim). The final renderer is native Metal implemented
// directly at the cIGZGDriver / SC4DrawContext level. Do not extend this file. It has been compile-checked only.
//
// MetalGL — a Metal implementation of IGLApi: the fixed-function OpenGL 1.x/2.x subset the DX7 shim drives
// (matrix stacks, two texture units with TexEnv combine, vertex colours, fog, alpha test, blending, depth, culling,
// client vertex arrays, buffer objects, S3TC and packed 16-bit textures, glReadPixels / glDrawPixels) turned into Metal
// render passes. No OpenGL anywhere: the game's GL calls are translated at the seam.
//
// Design notes (performance):
//  * one runtime-compiled "uber" shader; pipelines are cached per (blend, colour mask), depth states and samplers per key;
//  * all per-draw state travels in a single constant block via setVertexBytes / setFragmentBytes (no buffer churn);
//  * client-array vertices are gathered into a fixed 48-byte layout in a ring buffer (3 frames deep), index data too;
//  * matrices are simd types (NEON on arm64, SSE/AVX on x86_64) - there is no hand-written assembly;
//  * one command buffer per frame; passes are only split by glClear or read-backs.
#pragma once
#include <cstdint>
#include <memory>

#include "render/GLApi.h"

namespace sgl {

class MetalGL : public IGLApi {
public:
    // Creates the device and pipelines. `layer` (a CAMetalLayer*, may be null for headless use) receives presented frames.
    static std::unique_ptr<MetalGL> Create(void* caMetalLayer, std::uint32_t width, std::uint32_t height);
    ~MetalGL() override;
    void Resize(std::uint32_t width, std::uint32_t height);
    // Ends the frame: copies the colour target to the layer's drawable (if any) and submits.
    void Present();
    // Submits pending work and waits (used by read-backs and tests).
    void Finish() override;
    std::uint32_t DrawCallCount() const;   // draws since creation (for tests / stats)
    std::uint32_t PipelineCount() const;

    struct Impl;   // public so the implementation file's helpers can see it

    // ---- IGLApi ----
    void Enable(GLenum cap) override;
    void Disable(GLenum cap) override;
    bool IsEnabled(GLenum cap) override;
    void GetIntegerv(GLenum pname, GLint* out) override;
    void GetBooleanv(GLenum pname, std::uint8_t* out) override;
    const char* GetString(GLenum name) override;
    void PixelStorei(GLenum pname, GLint value) override;
    void PushAttrib(GLuint mask) override;
    void PopAttrib() override;
    void MatrixMode(GLenum mode) override;
    void LoadIdentity() override;
    void LoadMatrixf(const float* m) override;
    void MultMatrixf(const float* m) override;
    void PushMatrix() override;
    void PopMatrix() override;
    void Ortho(double l, double r, double b, double t, double n, double f) override;
    void Color4f(float r, float g, float b, float a) override;
    void RasterPos3f(float x, float y, float z) override;
    void WindowPos2i(GLint x, GLint y) override;
    void BlendFunc(GLenum src, GLenum dst) override;
    void AlphaFunc(GLenum func, float ref) override;
    void Materialfv(GLenum face, GLenum pname, const float* v) override;
    void ColorMaterial(GLenum face, GLenum mode) override;
    void ShadeModel(GLenum mode) override;
    void PolygonMode(GLenum face, GLenum mode) override;
    void FrontFace(GLenum mode) override;
    void CullFace(GLenum mode) override;
    void Fogfv(GLenum pname, const float* v) override;
    void Fogi(GLenum pname, GLint v) override;
    void LightModelfv(GLenum pname, const float* v) override;
    void ColorMask(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) override;
    void DepthMask(std::uint8_t flag) override;
    void DepthFunc(GLenum func) override;
    void StencilMask(GLuint mask) override;
    void ClearColor(float r, float g, float b, float a) override;
    void ClearDepth(double d) override;
    void ClearStencil(GLint s) override;
    void Clear(GLuint mask) override;
    void ReadBuffer(GLenum mode) override;
    void DrawBuffer(GLenum mode) override;
    void CopyPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum type) override;
    void DrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const void* data) override;
    void ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data) override;
    void ActiveTexture(GLenum unit) override;
    void ClientActiveTexture(GLenum unit) override;
    void TexEnvi(GLenum target, GLenum pname, GLint v) override;
    void TexEnvf(GLenum target, GLenum pname, float v) override;
    void TexEnvfv(GLenum target, GLenum pname, const float* v) override;
    void TexParameteri(GLenum target, GLenum pname, GLint v) override;
    void GenTextures(GLsizei n, GLuint* ids) override;
    void DeleteTextures(GLsizei n, const GLuint* ids) override;
    void BindTexture(GLenum target, GLuint id) override;
    void TexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLint border, GLenum format,
                    GLenum type, const void* data) override;
    void TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                       const void* data) override;
    void CompressedTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLsizei w, GLsizei h, GLint border,
                              GLsizei size, const void* data) override;
    void CompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                                 GLsizei size, const void* data) override;
    void GenBuffers(GLsizei n, GLuint* ids) override;
    void DeleteBuffers(GLsizei n, const GLuint* ids) override;
    void BindBuffer(GLenum target, GLuint id) override;
    void BufferData(GLenum target, std::ptrdiff_t size, const void* data, GLenum usage) override;
    void* MapBuffer(GLenum target, GLenum access) override;
    void UnmapBuffer(GLenum target) override;
    void BufferParameteriAPPLE(GLenum, GLenum, GLint) override {}
    void FlushMappedBufferRangeAPPLE(GLenum, std::ptrdiff_t, std::ptrdiff_t) override {}
    void GenVertexArraysAPPLE(GLsizei n, GLuint* ids) override;
    void BindVertexArrayAPPLE(GLuint) override {}
    void VertexArrayRangeAPPLE(GLsizei, void*) override {}
    void VertexArrayParameteriAPPLE(GLenum, GLint) override {}
    void FlushVertexArrayRangeAPPLE(GLsizei, void*) override {}
    void EnableClientState(GLenum array) override;
    void DisableClientState(GLenum array) override;
    void VertexPointer(GLint size, GLenum type, GLsizei stride, const void* p) override;
    void ColorPointer(GLint size, GLenum type, GLsizei stride, const void* p) override;
    void TexCoordPointer(GLint size, GLenum type, GLsizei stride, const void* p) override;
    void DrawArrays(GLenum mode, GLint first, GLsizei count) override;
    void DrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) override;
    void GenProgramsARB(GLsizei n, GLuint* ids) override;
    void BindProgramARB(GLenum, GLuint) override {}
    void ProgramStringARB(GLenum, GLenum, GLsizei, const void*) override {}
    void DeleteProgramsARB(GLsizei, const GLuint*) override {}

private:
    MetalGL();
    std::unique_ptr<Impl> impl_;
};

}  // namespace sgl
