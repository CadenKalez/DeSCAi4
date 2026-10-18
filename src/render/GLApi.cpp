#include "render/GLApi.h"

#include <vector>

namespace sgl {

namespace {
IGLApi* g_gl = nullptr;

class CNullGL final : public IGLApi {
public:
    void Enable(GLenum) override {}
    void Disable(GLenum) override {}
    bool IsEnabled(GLenum) override { return false; }
    void GetIntegerv(GLenum, GLint* out) override { out[0] = out[1] = out[2] = out[3] = 0; }
    void GetBooleanv(GLenum, std::uint8_t* out) override { out[0] = out[1] = out[2] = out[3] = 0; }
    const char* GetString(GLenum) override { return ""; }
    void PixelStorei(GLenum, GLint) override {}
    void PushAttrib(GLuint) override {}
    void PopAttrib() override {}
    void Finish() override {}
    void MatrixMode(GLenum) override {}
    void LoadIdentity() override {}
    void LoadMatrixf(const float*) override {}
    void MultMatrixf(const float*) override {}
    void PushMatrix() override {}
    void PopMatrix() override {}
    void Ortho(double, double, double, double, double, double) override {}
    void Color4f(float, float, float, float) override {}
    void RasterPos3f(float, float, float) override {}
    void WindowPos2i(GLint, GLint) override {}
    void BlendFunc(GLenum, GLenum) override {}
    void AlphaFunc(GLenum, float) override {}
    void Materialfv(GLenum, GLenum, const float*) override {}
    void ColorMaterial(GLenum, GLenum) override {}
    void ShadeModel(GLenum) override {}
    void PolygonMode(GLenum, GLenum) override {}
    void FrontFace(GLenum) override {}
    void CullFace(GLenum) override {}
    void Fogfv(GLenum, const float*) override {}
    void Fogi(GLenum, GLint) override {}
    void LightModelfv(GLenum, const float*) override {}
    void ColorMask(std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void DepthMask(std::uint8_t) override {}
    void DepthFunc(GLenum) override {}
    void StencilMask(GLuint) override {}
    void ClearColor(float, float, float, float) override {}
    void ClearDepth(double) override {}
    void ClearStencil(GLint) override {}
    void Clear(GLuint) override {}
    void ReadBuffer(GLenum) override {}
    void DrawBuffer(GLenum) override {}
    void CopyPixels(GLint, GLint, GLsizei, GLsizei, GLenum) override {}
    void DrawPixels(GLsizei, GLsizei, GLenum, GLenum, const void*) override {}
    void ReadPixels(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) override {}
    void ActiveTexture(GLenum) override {}
    void ClientActiveTexture(GLenum) override {}
    void TexEnvi(GLenum, GLenum, GLint) override {}
    void TexEnvf(GLenum, GLenum, float) override {}
    void TexEnvfv(GLenum, GLenum, const float*) override {}
    void TexParameteri(GLenum, GLenum, GLint) override {}
    void GenTextures(GLsizei n, GLuint* ids) override { for (GLsizei i = 0; i < n; ++i) ids[i] = ++nextId_; }
    void DeleteTextures(GLsizei, const GLuint*) override {}
    void BindTexture(GLenum, GLuint) override {}
    void TexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) override {}
    void TexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) override {}
    void CompressedTexImage2D(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*) override {}
    void CompressedTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void*) override {}
    void GenBuffers(GLsizei n, GLuint* ids) override { for (GLsizei i = 0; i < n; ++i) ids[i] = ++nextId_; }
    void DeleteBuffers(GLsizei, const GLuint*) override {}
    void BindBuffer(GLenum, GLuint) override {}
    void BufferData(GLenum, std::ptrdiff_t size, const void*, GLenum) override { scratch_.assign(size > 0 ? size : 0, 0); }
    void* MapBuffer(GLenum, GLenum) override { return scratch_.data(); }
    void UnmapBuffer(GLenum) override {}
    void BufferParameteriAPPLE(GLenum, GLenum, GLint) override {}
    void FlushMappedBufferRangeAPPLE(GLenum, std::ptrdiff_t, std::ptrdiff_t) override {}
    void GenVertexArraysAPPLE(GLsizei n, GLuint* ids) override { for (GLsizei i = 0; i < n; ++i) ids[i] = ++nextId_; }
    void BindVertexArrayAPPLE(GLuint) override {}
    void VertexArrayRangeAPPLE(GLsizei, void*) override {}
    void VertexArrayParameteriAPPLE(GLenum, GLint) override {}
    void FlushVertexArrayRangeAPPLE(GLsizei, void*) override {}
    void EnableClientState(GLenum) override {}
    void DisableClientState(GLenum) override {}
    void VertexPointer(GLint, GLenum, GLsizei, const void*) override {}
    void ColorPointer(GLint, GLenum, GLsizei, const void*) override {}
    void TexCoordPointer(GLint, GLenum, GLsizei, const void*) override {}
    void DrawArrays(GLenum, GLint, GLsizei) override {}
    void DrawElements(GLenum, GLsizei, GLenum, const void*) override {}
    void GenProgramsARB(GLsizei n, GLuint* ids) override { for (GLsizei i = 0; i < n; ++i) ids[i] = ++nextId_; }
    void BindProgramARB(GLenum, GLuint) override {}
    void ProgramStringARB(GLenum, GLenum, GLsizei, const void*) override {}
    void DeleteProgramsARB(GLsizei, const GLuint*) override {}

private:
    GLuint nextId_ = 0;
    std::vector<std::uint8_t> scratch_;
};
}  // namespace

IGLApi* GL() { return g_gl ? g_gl : NullGL(); }
void SetGL(IGLApi* gl) { g_gl = gl; }
IGLApi* NullGL() {
    static CNullGL instance;
    return &instance;
}

}  // namespace sgl
