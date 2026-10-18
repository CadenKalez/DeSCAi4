#include "render/D3DDevice7.h"

#include <cstring>

#include "render/FastATIVertexProgram.h"
#include "render/GLApi.h"
#include "render/MacOpenGL.h"

namespace sgl {

namespace {
// aOpenGLBlends, aOpenGLCmpFuncs, aOpenGLPrimitives (0x100a27950, 0x100a27990, 0x100a279e0), indexed by the D3D enum.
const std::uint32_t kBlends[16] = {0, 0, 1, 0x300, 0x301, 0x302, 0x303, 0x304, 0x305, 0x306, 0x307, 0x308, 0, 0, 0, 0};
const std::uint32_t kCmpFuncs[12] = {0, 0x200, 0x201, 0x202, 0x203, 0x204, 0x205, 0x206, 0x207, 0, 0, 0};
const std::uint32_t kPrimitives[7] = {0, 0, 1, 3, 4, 5, 6};
// (the original indexes these without checks; out-of-range values read neighbouring data, here they map to 0)
std::uint32_t Lookup(const std::uint32_t* table, std::size_t count, std::uint32_t index) { return index < count ? table[index] : 0; }
const _GUID kNoKey{};
constexpr std::uint32_t kStageBase = 0;

const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}  // namespace

CDirect3DDevice7::CDirect3DDevice7(IDirectDrawSurface7* target, IDirectDrawSurface7* zbuffer, const _D3DDeviceDesc7* desc,
                                   const std::vector<_DDPIXELFORMAT>* formats) {
    (void)kStageBase;
    target_ = target;
    zbuffer_ = zbuffer;
    std::memcpy(&desc_, desc, sizeof desc_);
    std::memset(&viewport_, 0xcc, sizeof viewport_);
    std::memset(reserved150_, 0xcc, sizeof reserved150_);
    std::memset(&viewDirty_, 0xcc, 0x90);   // +0x174 .. +0x204: both matrices and their flags, both unit selectors
    for (CTextureStage& stage : stages_) {
        stage.enabled = static_cast<std::int32_t>(kUnset);
        stage.texture = nullptr;
        std::memset(&stage.cameraSpace, 0xcc, 0x48);   // +0x10 .. +0x57: flags and the 64-byte matrix
        stage.rgbScale = stage.alphaScale = 0.0f;
        std::memset(stage.state, 0xcc, sizeof stage.state);
        GetDefaultOpenGLState(&stage.gl);
    }
    formats_ = *formats;
    blendDirty_ = 1;
    alphaDirty_ = 1;
    std::memset(renderState_, 0xcc, sizeof renderState_);
    GetDefaultOpenGLState(&gl_);
    // (the zeroed header fields are overwritten by the 0xcc fill that follows in the original)
    diffuse_[0] = diffuse_[1] = diffuse_[2] = diffuse_[3] = -1.0f;
    ambient_[0] = ambient_[1] = ambient_[2] = ambient_[3] = -1.0f;
    IGLApi* g = GL();
    if (matrixMode_ != 0x1700) {
        matrixMode_ = 0x1700;
        g->MatrixMode(0x1700);
    }
    _D3DMATRIX identity;
    std::memcpy(identity.m, kIdentity, sizeof kIdentity);
    SetTransform(static_cast<_D3DTRANSFORMSTATETYPE>(1), &identity);
    SetTransform(static_cast<_D3DTRANSFORMSTATETYPE>(3), &identity);
    g->PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    g->PixelStorei(gl::PACK_ALIGNMENT, 1);
    g->PixelStorei(gl::UNPACK_CLIENT_STORAGE_APPLE, 1);
    for (std::uint32_t unitEnum = 0x84c1; unitEnum != 0x84bf; --unitEnum) {
        const std::uint32_t unit = unitEnum - 0x84c0;
        std::uint32_t current = unit;
        if (unit != activeTexture_) {
            activeTexture_ = unit;
            g->ActiveTexture(unitEnum);
            current = activeTexture_;
        }
        CTextureStage& stage = stages_[current];
        std::memcpy(stage.matrix, kIdentity, sizeof kIdentity);
        stage.matrixDirty = 1;
        g->TexEnvi(0x2300, 0x2200, 0x8570);
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(1), unitEnum == 0x84c0 ? 4 : 1);
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(2), 2);
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(3), 1);
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(4), unitEnum == 0x84c0 ? 2 : 1);
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(5), 2);
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(6), 1);
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(0xb), 0);
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(0xc), 1);
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(0x10), 1);
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(0x11), 1);
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(0x12), 1);
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(0x18), 0);
        if (stage.enabled != 0) {
            stage.enabled = 0;
            g->Disable(gl::TEXTURE_2D);
        }
    }
    static const std::uint32_t kInitialStates[][2] = {
        {0x17, 4}, {0x13, 2}, {0x14, 1}, {0xe, 1}, {0x19, 8}, {0x18, 0x80}, {9, 2}, {0x3c, 0xffffffff}, {0x8c, 3},
        {0x22, 0}, {0x26, 0x3f800000}, {0x24, 0}, {0x25, 0x3f800000}, {0x89, 1}, {0x8b, 0}, {0x91, 1}, {0x93, 1},
        {0x16, 3}, {0x1c, 0}, {7, 1}, {0x2f, 0}, {0x1a, 0}, {0x1b, 0}, {0xf, 0}, {8, 3}};
    for (const auto& s : kInitialStates) SetRenderState(static_cast<_D3DRENDERSTATETYPE>(s[0]), s[1]);
    if (MacOpenGL::IsFastATICard()) FastATIVertexProgram::Init();
    AddRef();
}

CDirect3DDevice7::~CDirect3DDevice7() {
    if (MacOpenGL::IsFastATICard()) FastATIVertexProgram::Shutdown();
}

std::uint32_t CDirect3DDevice7::QueryInterface(const _GUID&, void** out) {
    *out = this;
    AddRef();
    return 0;
}

std::int32_t CDirect3DDevice7::Release() {
    const std::int32_t count = --refCount_;
    if (count == 0) delete this;
    return count;
}

std::uint32_t CDirect3DDevice7::GetCaps(_D3DDeviceDesc7* out) {
    std::memcpy(out, &desc_, sizeof desc_);
    return 0;
}

std::uint32_t CDirect3DDevice7::EnumTextureFormats(EnumTextureFormatsCallback callback, void* context) {
    for (std::size_t i = 0; i < formats_.size(); ++i) callback(&formats_[i], context);
    return 0;
}

std::uint32_t CDirect3DDevice7::GetRenderTarget(IDirectDrawSurface7** out) {
    *out = target_;
    target_->AddRef();
    return 0;
}

std::uint32_t CDirect3DDevice7::Clear(std::uint32_t, _D3DRECT*, std::uint32_t flags, std::uint32_t color, float z, std::uint32_t stencil) {
    IGLApi* g = GL();
    g->PushAttrib(0x4500);
    std::uint32_t mask = 0;
    if (flags & 1) {
        g->ColorMask(1, 1, 1, 1);
        g->Disable(gl::DITHER);
        g->ClearColor(static_cast<float>(static_cast<double>(static_cast<float>(color >> 16 & 0xff)) / 255.0),
                      static_cast<float>(static_cast<double>(static_cast<float>(color >> 8 & 0xff)) / 255.0),
                      static_cast<float>(static_cast<double>(static_cast<float>(color & 0xff)) / 255.0),
                      static_cast<float>(static_cast<double>(static_cast<float>(color >> 24)) / 255.0));
        mask = 0x4000;
    }
    if (flags & 2) {
        g->DepthMask(1);
        g->Enable(gl::DEPTH_TEST);
        g->DepthFunc(0x207);
        mask |= 0x100;
        g->ClearDepth(static_cast<double>(z));
    }
    if (flags & 4) {
        mask |= 0x400;
        g->StencilMask(0xffffffff);
        g->ClearStencil(static_cast<std::int32_t>(stencil));
    }
    g->Clear(mask);
    g->PopAttrib();
    return 0;
}

std::uint32_t CDirect3DDevice7::GetViewport(_D3DVIEWPORT7* out) {
    *out = viewport_;
    return 0;
}

std::uint32_t CDirect3DDevice7::SetViewport(_D3DVIEWPORT7* viewport) {
    viewport_ = *viewport;
    return 0;
}

std::uint32_t CDirect3DDevice7::SetMaterial(_D3DMATERIAL7* material) {
    SetAmbientMaterialColor(&material->ambient.r);
    SetDiffuseMaterialColor(&material->diffuse.r);
    return 0;
}

void CDirect3DDevice7::SetAmbientMaterialColor(const float* color) {
    if (std::memcmp(ambient_, color, 16) != 0) {
        std::memcpy(ambient_, color, 16);
        GL()->Materialfv(gl::FRONT_AND_BACK, gl::AMBIENT, color);
    }
}

void CDirect3DDevice7::SetDiffuseMaterialColor(const float* color) {
    if (std::memcmp(diffuse_, color, 16) != 0) {
        std::memcpy(diffuse_, color, 16);
        GL()->Materialfv(gl::FRONT_AND_BACK, gl::DIFFUSE, color);
    }
}

std::uint32_t CDirect3DDevice7::GetRenderState(_D3DRENDERSTATETYPE type, std::uint32_t* out) {
    const std::uint32_t state = type;
    if (state < 0x1d) {
        std::uint32_t off = 0;
        if (((0x18008080u >> (state & 0x1f)) & 1) == 0) {
            if (state != 0x16) return 0;
            off = 1;   // cull mode: 1 is "none"
        }
        *out = renderState_[state] != off ? 1 : 0;
    }
    return 0;
}

std::uint32_t CDirect3DDevice7::SetTransform(_D3DTRANSFORMSTATETYPE type, _D3DMATRIX* matrix) {
    const std::uint32_t t = type;
    if (t < 0x10) {
        if (t == 1) {
            if (std::memcmp(view_, matrix, 0x40) != 0) {
                std::memcpy(view_, matrix, 0x40);
                viewDirty_ = 1;
            }
        } else if (t == 3) {
            if (std::memcmp(proj_, matrix, 0x40) != 0) {
                std::memcpy(proj_, matrix, 0x40);
                projDirty_ = 1;
            }
        }
    } else if (t == 0x10) {
        CTextureStage& s = stages_[0];
        if (std::memcmp(s.matrix, matrix, 0x40) != 0) {
            std::memcpy(s.matrix, matrix, 0x40);
            s.matrixDirty = 1;
        }
    } else if (t == 0x11) {
        CTextureStage& s = stages_[1];
        if (std::memcmp(s.matrix, matrix, 0x40) != 0) {
            std::memcpy(s.matrix, matrix, 0x40);
            s.matrixDirty = 1;
        }
    }
    return 0;
}

// Applies one render state to GL, or marks the deferred groups (blend, alpha test) for UpdateRenderingState.
std::uint32_t CDirect3DDevice7::SetRenderState(_D3DRENDERSTATETYPE typeEnum, std::uint32_t value) {
    const std::uint32_t type = typeEnum;
    if (type >= 256) return 0;   // (the original indexes past its table)
    IGLApi* g = GL();
    if (renderState_[type] == value) return 0;
    renderState_[type] = value;
    auto setCap = [&](GLenum cap, bool on) { if (on) g->Enable(cap); else g->Disable(cap); };
    auto colorOf = [&](float out[4]) {
        out[0] = static_cast<float>(value >> 16 & 0xff) / 255.0f;
        out[1] = static_cast<float>(value >> 8 & 0xff) / 255.0f;
        out[2] = static_cast<float>(value & 0xff) / 255.0f;
        out[3] = static_cast<float>(value >> 24) / 255.0f;
    };
    switch (type) {
    case 7: setCap(gl::DEPTH_TEST, value != 0); break;
    case 8: {
        std::uint32_t mode = 0x1b02;
        if (value != 3) mode = (value == 2 ? 1u : 0u) | 0x1b00;
        g->PolygonMode(gl::FRONT_AND_BACK, mode);
        break;
    }
    case 9: g->ShadeModel(0x1d01 - (renderState_[9] == 1 ? 1u : 0u)); break;
    case 0xe: g->DepthMask(value != 0); break;
    case 0xf: case 0x18: case 0x19: alphaDirty_ = 1; break;
    case 0x13: case 0x14: case 0x1b: blendDirty_ = 1; break;
    case 0x16:
        if (value != 1) {
            g->Enable(gl::CULL_FACE);
            g->FrontFace((value == 2 ? 1u : 0u) | 0x900);
            g->CullFace(0x405);
        } else {
            g->Disable(gl::CULL_FACE);
        }
        break;
    case 0x17: g->DepthFunc(Lookup(kCmpFuncs, 12, value)); break;
    case 0x1a: setCap(gl::DITHER, value != 0); break;
    case 0x1c: setCap(gl::FOG, value != 0); break;
    case 0x22: {
        float c[4];
        colorOf(c);
        g->Fogfv(0xb66, c);
        break;
    }
    case 0x24: case 0x25: case 0x26: {
        float v;
        std::memcpy(&v, &value, 4);
        g->Fogfv(type == 0x24 ? 0xb63 : type == 0x25 ? 0xb64 : 0xb62, &v);
        break;
    }
    case 0x2f:
        if (value == 0) g->Disable(0x8037);
        break;
    case 0x89: setCap(gl::LIGHTING, value != 0); break;
    case 0x8b: {
        float c[4];
        colorOf(c);
        g->LightModelfv(0xb53, c);
        break;
    }
    case 0x8c: g->Fogi(0xb65, 0x2601); break;
    case 0x91: {   // diffuse material source; interacts with the ambient source (state 0x93)
        const std::uint32_t other = renderState_[0x93];
        if (value != 1) {
            if (other != kUnset) {
                if (other == 1) {
                    g->ColorMaterial(gl::FRONT_AND_BACK, gl::AMBIENT);
                    g->Enable(gl::COLOR_MATERIAL);
                    break;
                }
                if (other != 0) break;
            }
            g->Disable(gl::COLOR_MATERIAL);
        } else {
            if (other == kUnset || other == 0) g->ColorMaterial(gl::FRONT_AND_BACK, gl::DIFFUSE);
            else if (other == 1) g->ColorMaterial(gl::FRONT_AND_BACK, 0x1602);
            g->Enable(gl::COLOR_MATERIAL);
        }
        break;
    }
    case 0x93: {   // ambient material source; interacts with the diffuse source (state 0x91)
        const std::uint32_t other = renderState_[0x91];
        if (value == 1) {
            if (other == kUnset || other == 0) g->ColorMaterial(gl::FRONT_AND_BACK, gl::AMBIENT);
            else if (other == 1) g->ColorMaterial(gl::FRONT_AND_BACK, 0x1602);
            g->Enable(gl::COLOR_MATERIAL);
        } else {
            if (other != kUnset) {
                if (other == 1) {
                    g->ColorMaterial(gl::FRONT_AND_BACK, gl::DIFFUSE);
                    g->Enable(gl::COLOR_MATERIAL);
                    break;
                }
                if (other != 0) break;
            }
            g->Disable(gl::COLOR_MATERIAL);
        }
        break;
    }
    case 0x3c: {   // texture factor -> every unit's TexEnv colour
        float c[4];
        colorOf(c);
        for (std::uint32_t unitEnum = 0x84c1; unitEnum != 0x84bf; --unitEnum) {
            if (unitEnum - 0x84c0 != activeTexture_) {
                activeTexture_ = unitEnum - 0x84c0;
                g->ActiveTexture(unitEnum);
            }
            g->TexEnvfv(0x2300, 0x2201, c);
        }
        break;
    }
    default: break;
    }
    return 0;
}

std::uint32_t CDirect3DDevice7::SetTexture(std::uint32_t unit, IDirectDrawSurface7* texture) {
    if (unit < 2) {
        IGLApi* g = GL();
        if (activeTexture_ != unit) {
            activeTexture_ = unit;
            g->ActiveTexture(unit + 0x84c0);
            unit = activeTexture_;
        }
        OpenGLTextureState ts{};
        GetDefaultOpenGLTexState(gl::TEXTURE_2D, &ts.tex);
        CTextureStage& stage = stages_[unit];
        if (stage.texture) stage.texture->Release();
        if (!texture) {
            if (stage.enabled != 0) {
                stage.enabled = 0;
                g->Disable(gl::TEXTURE_2D);
            }
        } else {
            texture->AddRef();
            if (stage.enabled != 1) {
                stage.enabled = 1;
                g->Enable(gl::TEXTURE_2D);
            }
            texture->GetPrivateData(kNoKey, &ts, nullptr);
            if (ts.id == 0) {
                std::uint32_t id = 0;
                g->GenTextures(1, &id);
                ts.id = id;
                ts.dirty = 1;
                ts.uploaded = 0;
                g->BindTexture(gl::TEXTURE_2D, id);
                _DDSURFACEDESC2 d{};
                d.dwSize = 0x88;
                texture->GetSurfaceDesc(&d);
                ts.internalFormat = MacOpenGL::GetOpenGLFormat(d.ddpfPixelFormat).internalFormat;
            }
        }
        stage.SetTexture(ts, texture);
        if (texture) texture->SetPrivateData(kNoKey, &ts, sizeof ts, 0);
    }
    return 0;
}

std::uint32_t CDirect3DDevice7::SetTextureStageState(std::uint32_t unit, _D3DTEXTURESTAGESTATETYPE typeEnum, std::uint32_t value) {
    const std::uint32_t type = typeEnum;
    if (unit > 1 || type >= 32 || stages_[unit].state[type] == value) return 0;
    IGLApi* g = GL();
    if (activeTexture_ != unit) {
        activeTexture_ = unit;
        g->ActiveTexture(unit + 0x84c0);
    }
    const std::uint32_t u = activeTexture_;
    CTextureStage& stage = stages_[u];
    stage.state[type] = value;
    IDirectDrawSurface7* surface = stage.texture;
    OpenGLTextureState ts{};
    GetDefaultOpenGLTexState(gl::TEXTURE_2D, &ts.tex);
    if (surface) surface->GetPrivateData(kNoKey, &ts, nullptr);

    auto writeBack = [&] { surface->SetPrivateData(kNoKey, &ts, sizeof ts, 0); };
    // The three colour / alpha combine sources share one shape: D3DTA_* -> the GL source.
    auto setSource = [&](GLenum pname) {
        switch (value & 0xf) {
        case 0: g->TexEnvi(0x2300, pname, 0x8577); break;
        case 1: g->TexEnvi(0x2300, pname, 0x8578); break;
        case 2: g->TexEnvi(0x2300, pname, 0x1702); break;
        case 3: g->TexEnvi(0x2300, pname, 0x8576); break;
        default: break;
        }
    };
    // COLOROP / ALPHAOP: the combine function, plus the 1x / 2x / 4x scale.
    auto setOperation = [&](float& scale, GLenum combine, GLenum scalePname, bool colorExtras) {
        if (!(scale == 1.0f)) {
            scale = 1.0f;
            g->TexEnvf(0x2300, scalePname, 1.0f);
        }
        auto scaled = [&](float s) {
            if (!(scale == s)) {
                scale = s;
                g->TexEnvf(0x2300, scalePname, s);
            }
        };
        switch (value) {
        case 2: g->TexEnvi(0x2300, combine, 0x1e01); break;
        case 4: g->TexEnvi(0x2300, combine, 0x2100); break;
        case 5: g->TexEnvi(0x2300, combine, 0x2100); scaled(2.0f); break;
        case 6: g->TexEnvi(0x2300, combine, 0x2100); scaled(4.0f); break;
        case 7: g->TexEnvi(0x2300, combine, 0x104); break;
        case 8: g->TexEnvi(0x2300, combine, 0x8574); break;
        case 9: g->TexEnvi(0x2300, combine, 0x8574); scaled(2.0f); break;
        case 0xc: g->TexEnvi(0x2300, combine, 0x8575); break;
        case 0xd:
            g->TexEnvi(0x2300, combine, 0x8575);
            if (colorExtras) {
                g->TexEnvi(0x2300, 0x8582, 0x1702);
                g->TexEnvi(0x2300, 0x8592, 0x302);
            }
            break;
        case 0xe: case 0x10: g->TexEnvi(0x2300, combine, 0x8575); break;
        case 0x18: g->TexEnvi(0x2300, combine, 0x86ae); break;
        default: break;
        }
    };

    switch (type) {
    case 1:
        stage.SetEnabled(value != 1);
        setOperation(stage.rgbScale, 0x8571, 0x8573, true);
        break;
    case 2: setSource(0x8580); break;
    case 3: setSource(0x8581); break;
    case 4: setOperation(stage.alphaScale, 0x8572, 0xd1c, false); break;
    case 5: setSource(0x8588); break;
    case 6: setSource(0x8589); break;
    case 0xb:
        if ((value & 0xffff0000) == 0x20000) {
            stage.cameraSpace = 1;
            stage.matrixDirty = 1;
        } else if ((value & 0xffff0000) == 0) {
            stage.cameraSpace = 0;
            stage.matrixDirty = 1;
        }
        break;
    case 0xc:
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(0xd), value);
        SetTextureStageState(unit, static_cast<_D3DTEXTURESTAGESTATETYPE>(0xe), value);
        break;
    case 0xd:
    case 0xe: {
        const std::uint32_t wrap = (value == 1 ? 1u : 0u) | 0x2900;
        const GLenum pname = type == 0xd ? 0x2802 : 0x2803;
        std::uint32_t& cached = type == 0xd ? ts.tex.wrapS : ts.tex.wrapT;
        if (!surface) {
            g->TexParameteri(gl::TEXTURE_2D, pname, wrap);
            break;
        }
        if (cached == wrap) break;
        g->TexParameteri(gl::TEXTURE_2D, pname, wrap);
        cached = wrap;
        writeBack();
        break;
    }
    case 0x10: {
        const std::int32_t mag = 0x2601 - (value == 1 ? 1 : 0);
        if (!surface) {
            g->TexParameteri(gl::TEXTURE_2D, 0x2800, mag);
            break;
        }
        if (static_cast<std::int32_t>(ts.tex.magFilter) == mag) break;
        g->TexParameteri(gl::TEXTURE_2D, 0x2800, mag);
        ts.tex.magFilter = mag;
        writeBack();
        break;
    }
    case 0x11:
    case 0x12: {
        const std::int32_t minFilter = static_cast<std::int32_t>(stage.state[0x11]);
        std::int32_t mip = 1;
        if (stage.state[0x12] != kUnset) mip = static_cast<std::int32_t>(stage.state[0x12]);
        std::int32_t filter = 0;   // (an unknown mip filter leaves the original with a stray pointer value)
        const std::int32_t linear = minFilter == 2 ? 1 : 0;
        if (mip == 3) filter = 0x2701 + linear * 2;
        else if (mip == 2) filter = 0x2700 + linear * 2;
        else if (mip == 1) filter = linear | 0x2600;
        if (!surface) {
            g->TexParameteri(gl::TEXTURE_2D, 0x2801, filter);
            break;
        }
        if (static_cast<std::int32_t>(ts.tex.minFilter) == filter) break;
        g->TexParameteri(gl::TEXTURE_2D, 0x2801, filter);
        ts.tex.minFilter = filter;
        writeBack();
        break;
    }
    case 0x18:
        if (value == 0) {
            std::memset(stage.matrix, 0, sizeof stage.matrix);
            std::memcpy(stage.matrix, kIdentity, sizeof kIdentity);
            stage.matrixDirty = 1;
        }
        break;
    default: break;
    }
    return 0;
}


// Pushes everything the cached state says GL still has to be told: material colours, the view / projection matrices,
// blend and alpha-test setup, and for each enabled texture unit its matrix, mip-level limits and, when the surface was
// locked since the last draw, its pixels.
void CDirect3DDevice7::UpdateRenderingState() {
    IGLApi* g = GL();
    g->Materialfv(gl::FRONT_AND_BACK, gl::AMBIENT, ambient_);
    g->Materialfv(gl::FRONT_AND_BACK, gl::DIFFUSE, diffuse_);
    if (viewDirty_ != 0) {
        if (matrixMode_ != 0x1700) {
            matrixMode_ = 0x1700;
            g->MatrixMode(0x1700);
        }
        g->LoadMatrixf(view_);
        viewDirty_ = 0;
    }
    if (projDirty_ != 0) {
        if (matrixMode_ != 0x1701) {
            matrixMode_ = 0x1701;
            g->MatrixMode(0x1701);
        }
        g->LoadMatrixf(proj_);
        projDirty_ = 0;
    }
    if (blendDirty_ != 0) {
        if (renderState_[0x1b] == 0) {
            if (gl_.blendEnabled() == 1) {
                g->Disable(gl::BLEND);
                gl_.blendEnabled() = 0;
            }
        } else {
            std::uint32_t dst = 0;
            if (renderState_[0x14] != kUnset) dst = Lookup(kBlends, 16, renderState_[0x14]);
            g->BlendFunc(Lookup(kBlends, 16, renderState_[0x13]), dst);
            if (gl_.blendEnabled() == 0) {
                g->Enable(gl::BLEND);
                gl_.blendEnabled() = 1;
            }
        }
        blendDirty_ = 0;
    }
    if (alphaDirty_ != 0) {
        if (renderState_[0xf] == 0) {
            if (gl_.alphaTestEnabled() == 1) {
                g->Disable(gl::ALPHA_TEST);
                gl_.alphaTestEnabled() = 0;
            }
        } else {
            float ref = 0.0f;
            if (renderState_[0x18] != kUnset) ref = static_cast<float>(static_cast<double>(static_cast<float>(renderState_[0x18])) / 255.0);
            g->AlphaFunc(Lookup(kCmpFuncs, 12, renderState_[0x19]), ref);
            if (gl_.alphaTestEnabled() == 0) {
                g->Enable(gl::ALPHA_TEST);
                gl_.alphaTestEnabled() = 1;
            }
        }
        alphaDirty_ = 0;
    }
    for (std::uint32_t unit = 0; unit < 2; ++unit) {
        CTextureStage& stage = stages_[unit];
        if (stage.enabled == 0 || stage.texture == nullptr) continue;
        IDirectDrawSurface7* surface = stage.texture;
        OpenGLTextureState ts{};
        GetDefaultOpenGLTexState(gl::TEXTURE_2D, &ts.tex);
        surface->GetPrivateData(kNoKey, &ts, nullptr);
        const std::int32_t mipFilter = static_cast<std::int32_t>(stage.state[0x12]);
        std::uint32_t levels;
        bool changeLevels;
        if (mipFilter == 1) {
            changeLevels = false;
            levels = 1;
        } else {
            _DDSURFACEDESC2 d{};
            d.dwSize = 0x88;
            surface->GetSurfaceDesc(&d);
            if ((d.dwFlags & 0x20000) == 0) {
                changeLevels = false;
                levels = 1;
            } else {
                std::int32_t fullChain = 0;
                std::uint32_t w = d.dwWidth, h = d.dwHeight;
                if (h != 0 || w != 0) {
                    do {
                        if (w < 4 || h < 4) break;
                        const std::uint32_t either = h | w;
                        w >>= 1;
                        h >>= 1;
                        ++fullChain;
                        if ((either & 0xfffffffe) == 0) break;
                    } while (true);
                }
                levels = 1;
                changeLevels = true;
                if (d.dwMipMapCount != 1) {
                    changeLevels = static_cast<std::int32_t>(d.dwMipMapCount) != fullChain;
                    levels = d.dwMipMapCount;
                    if (!changeLevels) levels = 1;
                }
            }
        }
        if (stage.matrixDirty == 0 && ts.dirty == 0 && !changeLevels) continue;
        if (unit != activeTexture_) {
            activeTexture_ = unit;
            g->ActiveTexture(unit + gl::TEXTURE0);
        }
        const std::uint32_t current = activeTexture_;
        bool stateChanged;
        if (changeLevels) {
            if (!MacOpenGL::IsTextureLODSupported()) {
                if (static_cast<std::int32_t>(ts.tex.minFilter) == 0x2601) {
                    stateChanged = false;
                } else {
                    g->TexParameteri(gl::TEXTURE_2D, 0x2801, 0x2601);
                    ts.tex.minFilter = 0x2601;
                    stateChanged = true;
                }
            } else {
                const std::int32_t filter = 0x2601 - (mipFilter == 2 ? 1 : 0);
                if (levels == 1 && static_cast<std::int32_t>(ts.tex.minFilter) != filter) {
                    g->TexParameteri(gl::TEXTURE_2D, 0x2801, filter);
                    stateChanged = true;
                    ts.tex.minFilter = filter;
                } else {
                    stateChanged = false;
                }
                const std::int32_t maxLevel = static_cast<std::int32_t>(levels) - 1;
                if (static_cast<std::int32_t>(ts.tex.maxLevel) != maxLevel) {
                    g->TexParameteri(gl::TEXTURE_2D, 0x813d, maxLevel);
                    ts.tex.maxLevel = maxLevel;
                    stateChanged = true;
                }
            }
        } else {
            stateChanged = false;
        }
        CTextureStage& active = stages_[current];
        if (active.matrixDirty != 0) {
            if (matrixMode_ != 0x1702) {
                matrixMode_ = 0x1702;
                g->MatrixMode(0x1702);
            }
            g->LoadMatrixf(reinterpret_cast<const float*>(active.matrix));
            active.matrixDirty = 0;
            if (active.cameraSpace != 0) g->MultMatrixf(view_);
        }
        if (ts.dirty == 0) {
            if (stateChanged) surface->SetPrivateData(kNoKey, &ts, sizeof ts, 0);
            continue;
        }
        // The surface was locked since the last draw: send its pixels.
        _DDSURFACEDESC2 d{};
        d.dwSize = 0x88;
        surface->Lock(nullptr, &d, 0, nullptr);
        std::uint32_t height = d.dwHeight, width = d.dwWidth;
        const MacOpenGL::OpenGLTextureFormat fmt = MacOpenGL::GetOpenGLFormat(d.ddpfPixelFormat);
        const auto* data = static_cast<const std::uint8_t*>(d.lpSurface);
        if ((fmt.internalFormat & 0xfffffffc) == 0x83f0) {   // S3TC
            if (ts.uploaded == 0) {
                for (std::uint32_t level = 0; level < levels; ++level) {
                    if (width == 0) width = 1;
                    if (height == 0) height = 1;
                    const std::uint32_t size = fmt.GetImageSizeInBytes(width, height);
                    g->CompressedTexImage2D(gl::TEXTURE_2D, level, fmt.internalFormat, width, height, 0, size, data);
                    data += size;
                    width >>= 1;
                    height >>= 1;
                }
                ts.uploaded = 1;
            } else {
                for (std::uint32_t level = 0; level < levels; ++level) {
                    if (width == 0) width = 1;
                    if (height == 0) height = 1;
                    const std::uint32_t size = fmt.GetImageSizeInBytes(width, height);
                    g->CompressedTexSubImage2D(gl::TEXTURE_2D, level, 0, 0, width, height, fmt.internalFormat, size, data);
                    data += size;
                    width >>= 1;
                    height >>= 1;
                }
            }
        } else if (ts.uploaded == 0) {
            if (levels != 0) {
                std::uint32_t level = 0;
                for (;;) {
                    if (width == 0) width = 1;
                    if (height == 0) height = 1;
                    g->TexImage2D(gl::TEXTURE_2D, level, fmt.internalFormat, width, height, 0, fmt.format, fmt.type, data);
                    const std::uint32_t size = fmt.GetImageSizeInBytes(width, height);
                    if (levels <= ++level) break;
                    height >>= 1;
                    width >>= 1;
                    data += size;
                }
            }
            ts.uploaded = 1;
        } else {
            if (levels != 0) {
                if (levels == 1) {   // a single level goes through a pixel-unpack buffer
                    const std::uint32_t size = fmt.GetImageSizeInBytes(width, height);
                    if (ts.pixelBuffer == 0) {
                        g->GenBuffers(1, &ts.pixelBuffer);
                        g->BindBuffer(0x88ec, ts.pixelBuffer);
                        g->BufferData(0x88ec, size, data, 0x88e8);
                    } else {
                        g->BindBuffer(0x88ec, ts.pixelBuffer);
                        g->BufferData(0x88ec, size, nullptr, 0x88e8);
                        void* mapped = g->MapBuffer(0x88ec, 0x88b9);
                        std::memcpy(mapped, data, size);
                        g->UnmapBuffer(0x88ec);
                    }
                    data = nullptr;
                }
                for (std::uint32_t level = 0; level < levels; ++level) {
                    if (width == 0) width = 1;
                    if (height == 0) height = 1;
                    g->TexSubImage2D(gl::TEXTURE_2D, level, 0, 0, width, height, fmt.format, fmt.type, data);
                    const std::uint32_t size = fmt.GetImageSizeInBytes(width, height);
                    if (data) data += size;   // (null while reading from the pixel-unpack buffer)
                    width >>= 1;
                    height >>= 1;
                }
            }
            if (ts.pixelBuffer != 0) g->BindBuffer(0x88ec, 0);
        }
        surface->Unlock(nullptr);
        ts.dirty = 0;
        surface->SetPrivateData(kNoKey, &ts, sizeof ts, 0);
    }
}

namespace {
// Pointer + offset without pointer arithmetic: with a bound buffer object the "pointer" is just an offset.
inline const std::uint8_t* Advance(const std::uint8_t* p, std::int64_t off) {
    return reinterpret_cast<const std::uint8_t*>(reinterpret_cast<std::uintptr_t>(p) + static_cast<std::uintptr_t>(off));
}
inline std::int32_t TexCoordSet(const CTextureStage& stage) {
    const std::uint32_t index = static_cast<std::uint16_t>(stage.state[0xb]);
    return index < 2 ? static_cast<std::int32_t>(index) : 0;   // (an unset index reads far outside the buffer in the original)
}
}  // namespace

// Binds a vertex buffer's data through the APPLE vertex-array-range path: the buffer remembers which client state and
// pointers it set last, so nothing is re-sent when the same buffer is drawn again.
void CDirect3DDevice7::UpdateVarPtrs(CDirect3DVertexBuffer7* vb, std::uint32_t stride, const std::uint8_t* base) {
    IGLApi* g = GL();
    if (!vb->vertexArrayOn) {
        g->EnableClientState(gl::VERTEX_ARRAY);
        vb->vertexArrayOn = true;
    }
    if (vb->vertexPointer != base) {
        g->VertexPointer(3, gl::FLOAT, stride, base);
        vb->vertexPointer = base;
    }
    std::int32_t colorOffset = vb->diffuseOffset;
    if (colorOffset < 0) {
        if (vb->colorArrayOn) {
            g->DisableClientState(gl::COLOR_ARRAY);
            vb->colorArrayOn = false;
        }
    } else {
        if (!vb->colorArrayOn) {
            g->EnableClientState(gl::COLOR_ARRAY);
            vb->colorArrayOn = true;
            colorOffset = vb->diffuseOffset;
        }
        if (vb->colorPointer != Advance(base, colorOffset)) {
            g->ColorPointer(4, gl::UNSIGNED_BYTE, stride, Advance(base, colorOffset));
            vb->colorPointer = Advance(base, vb->diffuseOffset);
        }
    }
    for (std::uint32_t unit = 0; unit < 2; ++unit) {
        const CTextureStage& stage = stages_[unit];
        auto selectClientUnit = [&] {
            if (unit != clientActiveTexture_) {
                clientActiveTexture_ = unit;
                g->ClientActiveTexture(unit + gl::TEXTURE0);
            }
        };
        if (stage.enabled == 0) {
            if (vb->texArrayOn[unit]) {
                vb->texArrayOn[unit] = false;
                selectClientUnit();
                g->DisableClientState(gl::TEXTURE_COORD_ARRAY);
            }
        } else {
            std::int32_t size = 3;
            const std::uint8_t* pointer = base;
            if (stage.cameraSpace == 0) {
                size = vb->texSize[unit];
                pointer = Advance(base, vb->texOffset[TexCoordSet(stage)]);
            }
            if (!vb->texArrayOn[unit]) {
                vb->texArrayOn[unit] = true;
                selectClientUnit();
                g->EnableClientState(gl::TEXTURE_COORD_ARRAY);
            }
            if (vb->texPointer[unit] != pointer) {
                selectClientUnit();
                g->TexCoordPointer(size, gl::FLOAT, stride, pointer);
                vb->texPointer[unit] = pointer;
            }
        }
    }
}

// The same binding through the device's own array mirrors, for drivers without vertex-array ranges.
void CDirect3DDevice7::UpdateNonVarPtrs(CDirect3DVertexBuffer7* vb, std::uint32_t stride, const std::uint8_t* base) {
    IGLApi* g = GL();
    if (vertexArray_.enabled != 1) {
        vertexArray_.enabled = 1;
        g->EnableClientState(gl::VERTEX_ARRAY);
    }
    vertexArray_.SetPointer(3, gl::FLOAT, stride, base);
    std::int32_t colorOffset = vb->diffuseOffset;
    if (colorOffset < 0) {
        if (colorArray_.enabled != 0) {
            colorArray_.enabled = 0;
            g->DisableClientState(gl::COLOR_ARRAY);
        }
    } else {
        if (colorArray_.enabled != 1) {
            colorArray_.enabled = 1;
            g->EnableClientState(gl::COLOR_ARRAY);
            colorOffset = vb->diffuseOffset;
        }
        colorArray_.SetPointer(4, gl::UNSIGNED_BYTE, stride, Advance(base, colorOffset));
    }
    for (std::uint32_t unit = 0; unit < 2; ++unit) {
        CTextureStage& stage = stages_[unit];
        CTextureCoordArray& array = stage.coords;
        auto selectClientUnit = [&] {
            if (unit != clientActiveTexture_) {
                clientActiveTexture_ = unit;
                g->ClientActiveTexture(unit + gl::TEXTURE0);
            }
        };
        const std::int32_t enable = stage.enabled;
        if (array.enabled != enable) {
            selectClientUnit();
            array.SetEnable(gl::TEXTURE_COORD_ARRAY, enable);
        }
        if (enable != 0) {
            std::int32_t size = 3;
            const std::uint8_t* pointer = base;
            if (stage.cameraSpace == 0) {
                size = vb->texSize[unit];
                pointer = Advance(base, vb->texOffset[TexCoordSet(stage)]);
            }
            if (array.enabled != enable || array.size != size || array.type != gl::FLOAT ||
                array.stride != static_cast<std::int32_t>(stride) || array.pointer != pointer) {
                selectClientUnit();
                array.SetEnable(gl::TEXTURE_COORD_ARRAY, enable);
                array.SetPointer(size, gl::FLOAT, stride, pointer);
            }
        }
    }
}

namespace {
// Shared front half of the two vertex-buffer draws: returns the buffer after binding its data.
CDirect3DVertexBuffer7* BindForDraw(CDirect3DDevice7& device, IDirect3DVertexBuffer7* vb, std::uint32_t start) {
    IGLApi* g = GL();
    if (FastATIVertexProgram::bEnabled != 0) {
        FastATIVertexProgram::bEnabled = 0;
        g->Disable(gl::VERTEX_PROGRAM_ARB);
    }
    device.UpdateRenderingState();
    auto* buffer = dynamic_cast<CDirect3DVertexBuffer7*>(vb);
    if (!buffer) return nullptr;
    const std::uint8_t* storage = buffer->storage;
    const std::uint32_t stride = static_cast<std::uint32_t>(CDirect3DVertexBuffer7::GetFVFSize(buffer->desc.dwFVF));
    if (!MacOpenGL::IsVertexArraySupported()) {
        const std::uint8_t* pointer;
        if (buffer->bufferObject == 0) {
            pointer = storage + static_cast<std::uint32_t>(stride * start);
        } else {
            if (static_cast<std::int32_t>(buffer->bufferObject) != g_CurrVBO) {
                g->BindBuffer(gl::ARRAY_BUFFER, buffer->bufferObject);
                g_CurrVBO = static_cast<std::int32_t>(buffer->bufferObject);
            }
            pointer = reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(static_cast<std::uint32_t>(stride * start)));
        }
        device.UpdateNonVarPtrs(buffer, stride, pointer);
    } else {
        if (g_CurrVarId != static_cast<std::int32_t>(buffer->vertexArrayObject)) {
            g_CurrVarId = static_cast<std::int32_t>(buffer->vertexArrayObject);
            g->BindVertexArrayAPPLE(buffer->vertexArrayObject);
        }
        device.UpdateVarPtrs(buffer, stride, storage + static_cast<std::uint32_t>(stride * start));
    }
    return buffer;
}
}  // namespace

std::uint32_t CDirect3DDevice7::DrawPrimitiveVB(_D3DPRIMITIVETYPE type, IDirect3DVertexBuffer7* vb, std::uint32_t start, std::uint32_t count, std::uint32_t) {
    if (!BindForDraw(*this, vb, start)) return 0;
    GL()->DrawArrays(Lookup(kPrimitives, 7, type), 0, count);
    return 0;
}

std::uint32_t CDirect3DDevice7::DrawIndexedPrimitiveVB(_D3DPRIMITIVETYPE type, IDirect3DVertexBuffer7* vb, std::uint32_t start, std::uint32_t, std::uint16_t* indices, std::uint32_t indexCount, std::uint32_t) {
    if (!BindForDraw(*this, vb, start)) return 0;
    GL()->DrawElements(Lookup(kPrimitives, 7, type), indexCount, gl::UNSIGNED_SHORT, indices);
    return 0;
}

}  // namespace sgl
