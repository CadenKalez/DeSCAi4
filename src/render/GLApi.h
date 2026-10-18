// IGLApi — the OpenGL entry points the DX7 compatibility shim (CDirect3DDevice7, CDirectDrawSurface7,
// CDirect3DVertexBuffer7, MacOpenGL, FastATIVertexProgram) calls. The original links the system OpenGL framework and
// calls these directly; here they sit behind one interface so the recovered code is backend-neutral: a test installs a
// recording implementation, and the native renderer installs one that turns the fixed-function command stream into
// Metal work (see MetalRenderer). Method names follow the GL entry points; ARB / APPLE aliases that the original calls
// by different names but that mean the same thing share one method.
#pragma once
#include <cstddef>
#include <cstdint>

namespace sgl {

using GLenum = std::uint32_t;
using GLuint = std::uint32_t;
using GLint = std::int32_t;
using GLsizei = std::int32_t;

// The GL constants the shim uses, by their hex value in the game.
namespace gl {
constexpr GLenum TEXTURE_2D = 0xde1, TEXTURE_CUBE_MAP = 0x8513, SCISSOR_TEST = 0xc11, ALPHA_TEST = 0xbc0,
                 STENCIL_TEST = 0xb90, DEPTH_TEST = 0xb71, BLEND = 0xbe2, DITHER = 0xbd0, LIGHTING = 0xb50, FOG = 0xb60,
                 CULL_FACE = 0xb44, COLOR_MATERIAL = 0xb57, NORMALIZE = 0xba1, VERTEX_ARRAY = 0x8074, COLOR_ARRAY = 0x8076,
                 TEXTURE_COORD_ARRAY = 0x8078, VERTEX_PROGRAM_ARB = 0x8620;
constexpr GLenum MODELVIEW = 0x1700, PROJECTION = 0x1701, TEXTURE = 0x1702;
constexpr GLenum VIEWPORT = 0xba2, COLOR_WRITEMASK = 0xc23, STENCIL_WRITEMASK = 0xb98, DEPTH_WRITEMASK = 0xb72,
                 DEPTH_FUNC = 0xb74, ACTIVE_TEXTURE = 0x84e0, EXTENSIONS = 0x1f03;
constexpr GLenum UNPACK_ROW_LENGTH = 0xcf2, UNPACK_ALIGNMENT = 0xcf5, PACK_ROW_LENGTH = 0xd02, PACK_ALIGNMENT = 0xd05,
                 UNPACK_CLIENT_STORAGE_APPLE = 0x85b2;
constexpr GLenum TEXTURE0 = 0x84c0, ARRAY_BUFFER = 0x8892, ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr GLenum FRONT_AND_BACK = 0x408, AMBIENT = 0x1200, DIFFUSE = 0x1201;
constexpr GLenum UNSIGNED_BYTE = 0x1401, UNSIGNED_SHORT = 0x1403, FLOAT = 0x1406;
}  // namespace gl

class IGLApi {
public:
    virtual ~IGLApi() = default;
    // state
    virtual void Enable(GLenum cap) = 0;
    virtual void Disable(GLenum cap) = 0;
    virtual bool IsEnabled(GLenum cap) = 0;
    virtual void GetIntegerv(GLenum pname, GLint* out) = 0;
    virtual void GetBooleanv(GLenum pname, std::uint8_t* out) = 0;
    virtual const char* GetString(GLenum name) = 0;
    virtual void PixelStorei(GLenum pname, GLint value) = 0;
    virtual void PushAttrib(GLuint mask) = 0;
    virtual void PopAttrib() = 0;
    virtual void Finish() = 0;
    // matrices and raster position
    virtual void MatrixMode(GLenum mode) = 0;
    virtual void LoadIdentity() = 0;
    virtual void LoadMatrixf(const float* m) = 0;
    virtual void MultMatrixf(const float* m) = 0;
    virtual void PushMatrix() = 0;
    virtual void PopMatrix() = 0;
    virtual void Ortho(double l, double r, double b, double t, double n, double f) = 0;
    virtual void Color4f(float r, float g, float b, float a) = 0;
    virtual void RasterPos3f(float x, float y, float z) = 0;
    virtual void WindowPos2i(GLint x, GLint y) = 0;
    // fixed function
    virtual void BlendFunc(GLenum src, GLenum dst) = 0;
    virtual void AlphaFunc(GLenum func, float ref) = 0;     // NOTE argument order: (func, ref)
    virtual void Materialfv(GLenum face, GLenum pname, const float* v) = 0;
    virtual void ColorMaterial(GLenum face, GLenum mode) = 0;
    virtual void ShadeModel(GLenum mode) = 0;
    virtual void PolygonMode(GLenum face, GLenum mode) = 0;
    virtual void FrontFace(GLenum mode) = 0;
    virtual void CullFace(GLenum mode) = 0;
    virtual void Fogfv(GLenum pname, const float* v) = 0;
    virtual void Fogi(GLenum pname, GLint v) = 0;
    virtual void LightModelfv(GLenum pname, const float* v) = 0;
    virtual void ColorMask(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) = 0;
    virtual void DepthMask(std::uint8_t flag) = 0;
    virtual void DepthFunc(GLenum func) = 0;
    virtual void StencilMask(GLuint mask) = 0;
    virtual void ClearColor(float r, float g, float b, float a) = 0;
    virtual void ClearDepth(double d) = 0;
    virtual void ClearStencil(GLint s) = 0;
    virtual void Clear(GLuint mask) = 0;
    // framebuffer
    virtual void ReadBuffer(GLenum mode) = 0;
    virtual void DrawBuffer(GLenum mode) = 0;
    virtual void CopyPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum type) = 0;
    virtual void DrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const void* data) = 0;
    virtual void ReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data) = 0;
    // textures
    virtual void ActiveTexture(GLenum unit) = 0;
    virtual void ClientActiveTexture(GLenum unit) = 0;
    virtual void TexEnvi(GLenum target, GLenum pname, GLint v) = 0;
    virtual void TexEnvf(GLenum target, GLenum pname, float v) = 0;
    virtual void TexEnvfv(GLenum target, GLenum pname, const float* v) = 0;
    virtual void TexParameteri(GLenum target, GLenum pname, GLint v) = 0;
    virtual void GenTextures(GLsizei n, GLuint* ids) = 0;
    virtual void DeleteTextures(GLsizei n, const GLuint* ids) = 0;
    virtual void BindTexture(GLenum target, GLuint id) = 0;
    virtual void TexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLint border,
                            GLenum format, GLenum type, const void* data) = 0;
    virtual void TexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                               GLenum type, const void* data) = 0;
    virtual void CompressedTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLsizei w, GLsizei h,
                                      GLint border, GLsizei size, const void* data) = 0;
    virtual void CompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h,
                                         GLenum format, GLsizei size, const void* data) = 0;
    // buffers and vertex arrays
    virtual void GenBuffers(GLsizei n, GLuint* ids) = 0;
    virtual void DeleteBuffers(GLsizei n, const GLuint* ids) = 0;
    virtual void BindBuffer(GLenum target, GLuint id) = 0;
    virtual void BufferData(GLenum target, std::ptrdiff_t size, const void* data, GLenum usage) = 0;
    virtual void* MapBuffer(GLenum target, GLenum access) = 0;
    virtual void UnmapBuffer(GLenum target) = 0;
    virtual void BufferParameteriAPPLE(GLenum target, GLenum pname, GLint v) = 0;
    virtual void FlushMappedBufferRangeAPPLE(GLenum target, std::ptrdiff_t offset, std::ptrdiff_t size) = 0;
    virtual void GenVertexArraysAPPLE(GLsizei n, GLuint* ids) = 0;
    virtual void BindVertexArrayAPPLE(GLuint id) = 0;
    virtual void VertexArrayRangeAPPLE(GLsizei length, void* pointer) = 0;
    virtual void VertexArrayParameteriAPPLE(GLenum pname, GLint v) = 0;
    virtual void FlushVertexArrayRangeAPPLE(GLsizei length, void* pointer) = 0;
    virtual void EnableClientState(GLenum array) = 0;
    virtual void DisableClientState(GLenum array) = 0;
    virtual void VertexPointer(GLint size, GLenum type, GLsizei stride, const void* p) = 0;
    virtual void ColorPointer(GLint size, GLenum type, GLsizei stride, const void* p) = 0;
    virtual void TexCoordPointer(GLint size, GLenum type, GLsizei stride, const void* p) = 0;
    virtual void DrawArrays(GLenum mode, GLint first, GLsizei count) = 0;
    virtual void DrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) = 0;
    // ARB vertex programs (the "fast ATI" path)
    virtual void GenProgramsARB(GLsizei n, GLuint* ids) = 0;
    virtual void BindProgramARB(GLenum target, GLuint id) = 0;
    virtual void ProgramStringARB(GLenum target, GLenum format, GLsizei len, const void* text) = 0;
    virtual void DeleteProgramsARB(GLsizei n, const GLuint* ids) = 0;
};

// The process-wide GL implementation (the original calls the system library). Never null once set.
IGLApi* GL();
void SetGL(IGLApi* gl);

// A do-nothing implementation for headless use: queries answer "off" / zero, MapBuffer hands back a scratch block.
IGLApi* NullGL();

}  // namespace sgl
