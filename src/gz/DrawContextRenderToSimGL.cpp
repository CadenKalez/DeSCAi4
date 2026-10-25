#include "gz/DrawContextRenderToSimGL.h"

namespace {
// eGDTestFunc / eGDBlend / eGDDriverState / eGDVertexFormat values the original passes as literals.
constexpr gd::eGDTestFunc kGreater = 4;
constexpr gd::eGDBlend kSrcAlpha = 4, kInvSrcAlpha = 5;
constexpr gd::eGDVertexFormat kVertexFormat = gd::vf::V3F_C4UB_T2F;   // 10: x y z, ABGR colour, u v = cGZDrawVertex
constexpr int kVertexStride = 0x18;
constexpr int kMaxTriangleVertices = 0x7fe;   // a multiple of 3 below 2048
constexpr int kMaxQuadVertices = 0x800;
}  // namespace

cGZDrawContextRenderToSimGL::cGZDrawContextRenderToSimGL(cGZDrawContextRenderToHardware* engine, ISimGLDrawServices* services)
    : engine_(engine), services_(services) {}   // 0x1007b38ec

cGZDrawContextRenderToSimGL::~cGZDrawContextRenderToSimGL() {   // 0x1007b39c4
    if (driver_) driver_->Release();
    if (tiles_) tiles_->Release();
    if (cache_) cache_->Release();
}

// 0x1007b4588
bool cGZDrawContextRenderToSimGL::Init() {
    gd::cIGZGDriver* driver = services_ ? services_->GetGDriver() : nullptr;
    if (!driver) return false;
    if (driver != driver_) {
        driver->AddRef();
        gd::cIGZGDriver* old = driver_;
        driver_ = driver;
        if (old) old->Release();
    }
    firstUpdate_ = true;   // +0x118
    return true;
}

// 0x1007b466a: drops the driver; whatever state this context left in it is reported to the graphics system first.
void cGZDrawContextRenderToSimGL::Shutdown() {
    gd::cIGZGDriver* driver = driver_;
    if (!driver) return;
    driver_ = nullptr;
    driver->Release();
    if (modified_) {
        services_->DriverStateChanged();
        modified_ = false;
    }
    if (drawn_) {
        services_->ScreenDirty();
        drawn_ = false;
    }
}

// The driver states the UI draws with: no depth, source-alpha blending, alpha test greater than one half, modelview identity.
// 0x1007b3b5e (Resume) and the first UpdateState run the same calls.
void cGZDrawContextRenderToSimGL::ResetDriverStates() {
    driver_->Disable(gd::state::Blend);
    driver_->AlphaFunc(kGreater, 0.5f);
    driver_->Disable(gd::state::Texturing);
    driver_->Disable(gd::state::DepthTest);
    driver_->BlendFunc(kSrcAlpha, kInvSrcAlpha);
    driver_->MatrixMode(0);
    driver_->LoadIdentity();
}

// 0x1007b3abe: the engine's own Suspend has raised its dirty flags; this reports the driver state back.
void cGZDrawContextRenderToSimGL::Suspend() {
    if (modified_) {
        services_->DriverStateChanged();
        modified_ = false;
    }
    if (drawn_) {
        services_->ScreenDirty();
        drawn_ = false;
    }
}

// 0x1007b3b5e
void cGZDrawContextRenderToSimGL::Resume() {
    translucency_ = true;
    services_->DriverStateChanged();
    translucency_ = services_->TranslucencyAllowed();
    if (!translucency_) threshold_ = services_->TranslucencyThreshold();
    driver_->Disable(gd::state::Blend);
    driver_->Enable(gd::state::AlphaTest);
    driver_->AlphaFunc(kGreater, 0.5f);
    driver_->Disable(gd::state::Texturing);
    driver_->Disable(gd::state::DepthTest);
    driver_->BlendFunc(kSrcAlpha, kInvSrcAlpha);
    driver_->MatrixMode(0);
    driver_->LoadIdentity();
}

// 0x1007b4724
void cGZDrawContextRenderToSimGL::UpdateState() {
    modified_ = true;
    if (firstUpdate_) {
        firstUpdate_ = false;
        services_->DriverStateChanged();
        ResetDriverStates();
    }
    cIGZBuffer* source = engine_->BltSource();
    if (!source) driver_->Disable(gd::state::Texturing);
    else driver_->Enable(gd::state::Texturing);

    if (!engine_->AlphaBlend()) {
        driver_->Disable(gd::state::Blend);
        driver_->AlphaFunc(kGreater, 0.5f);
        if (!source || !source->GetTransparency()) {
            driver_->Disable(gd::state::AlphaTest);
            driver_->TexEnv(0, 0, engine_->BltModulate() ? 1 : 0);
            return;
        }
    } else {
        float reference;
        if (!translucency_ && !engine_->AlphaBlendStrong()) {
            // translucency is forced off: an alpha test against the system's threshold stands in for blending
            driver_->Disable(gd::state::Blend);
            reference = threshold_;
        } else {
            driver_->Enable(gd::state::Blend);
            reference = 0.0f;
        }
        driver_->AlphaFunc(kGreater, reference);
    }
    driver_->Enable(gd::state::AlphaTest);
    driver_->TexEnv(0, 0, engine_->BltModulate() ? 1 : 0);
}

// 0x1007b4950: the clip rectangle in the engine's top-left coordinates becomes a bottom-left driver viewport.
void cGZDrawContextRenderToSimGL::SetViewport(const cRZRect& viewport) {
    modified_ = true;
    driver_->SetViewport(static_cast<std::uint32_t>(viewport.left),
                         static_cast<std::uint32_t>(engine_->CanvasHeight() - viewport.bottom),
                         static_cast<std::uint32_t>(viewport.right - viewport.left),
                         static_cast<std::uint32_t>(viewport.bottom - viewport.top));
}

// 0x1007b498a
void cGZDrawContextRenderToSimGL::SetTransform(const float* matrix) {
    modified_ = true;
    driver_->MatrixMode(1);
    driver_->LoadMatrix(matrix);
}

// 0x1007b49ce: finds (or creates and attaches) the buffer's texture cache and its tile source.
bool cGZDrawContextRenderToSimGL::SetImage(cIGZBuffer* image) {
    if (cache_) {
        cIGZBufferHardwareCache* old = cache_;
        cache_ = nullptr;
        old->Release();
    }
    if (!image) return false;
    cIGZBufferHardwareCache* existing = image->GetHardwareCache();
    if (existing) existing->AddRef();
    cache_ = existing;
    if (!cache_) {
        auto* created = new cGZBufferSimGLCache(services_);
        created->AddRef();
        if (image->SetHardwareCache(created)) {
            created->AddRef();
            cache_ = created;
        }
        created->Release();
    }
    if (cache_) {
        if (tiles_) {
            cIGZBufferTileSource* old = tiles_;
            tiles_ = nullptr;
            old->Release();
        }
        void* source = nullptr;
        if (cache_->QueryInterface(kGZIID_cIGZBufferTileSource, &source)) {
            tiles_ = static_cast<cIGZBufferTileSource*>(source);
            return true;
        }
        cIGZBufferHardwareCache* old = cache_;
        cache_ = nullptr;
        old->Release();
    }
    if (tiles_) {
        cIGZBufferTileSource* old = tiles_;
        tiles_ = nullptr;
        old->Release();
    }
    return false;
}

// 0x1007b4b6c: refreshes the texture copy, then lists every tile as a texture followed by its pieces.
void cGZDrawContextRenderToSimGL::GetImageFragments(std::vector<cGZDrawImageTexture>& textures,
                                                    std::vector<cGZDrawImageFragment>& fragments) {
    if (!cache_ || !tiles_) return;
    static_cast<cGZBufferSimGLCache*>(cache_)->Refresh(services_->BufferCacheClock());
    const std::int32_t tileCount = tiles_->GetTileCount();
    for (std::int32_t i = 0; i < tileCount; ++i) {
        std::uint32_t texture = 0, pieceCount = 0;
        cRZTileSize size;
        const cRZSubTile* pieces = nullptr;
        tiles_->GetTile(static_cast<std::uint32_t>(i), texture, size, pieces, pieceCount);
        cGZDrawImageTexture entry;
        entry.id = texture;
        entry.fragmentCount = pieceCount;
        entry.width = static_cast<std::uint32_t>(size.width);
        entry.height = static_cast<std::uint32_t>(size.height);
        textures.push_back(entry);
        for (std::uint32_t p = 0; p < pieceCount; ++p, ++pieces) {
            cGZDrawImageFragment f;
            f.left = pieces->x;
            f.top = pieces->y;
            f.right = pieces->x + pieces->width;
            f.bottom = pieces->y + pieces->height;
            f.u0 = pieces->tileX;
            f.v0 = pieces->tileY;
            f.u1 = pieces->tileX + pieces->width;
            f.v1 = pieces->tileY + pieces->height;
            fragments.push_back(f);
        }
    }
}

// 0x1007b4cf6: stage 0 gets the texture; both filters are 1 when the blit is scaled (0 otherwise) and both wraps are 3 when the
// source is tiled (2 otherwise). Parameter types: 0 magnification, 1 minification, 2 wrap S, 3 wrap T.
void cGZDrawContextRenderToSimGL::SetTexture(std::uint32_t texture, bool scaled, bool tiled) {
    driver_->SetTexture(texture, 0);
    driver_->TexParameter(0, 1, scaled ? 1 : 0);   // minification filter
    driver_->TexParameter(0, 0, scaled ? 1 : 0);   // magnification filter
    driver_->TexParameter(0, 2, tiled ? 3 : 2);    // wrap S
    driver_->TexParameter(0, 3, tiled ? 3 : 2);    // wrap T
}

// 0x1007b4de2: client-array draws in chunks the driver accepts (2046 vertices = 682 triangles per call).
void cGZDrawContextRenderToSimGL::DrawTriangleList(cGZDrawVertex* vertices, int count) {
    while (count > 0) {
        const int n = count < kMaxTriangleVertices + 1 ? count : kMaxTriangleVertices;
        driver_->InterleavedArrays(kVertexFormat, kVertexStride, vertices);
        driver_->DrawArrays(gd::prim::Triangles, 0, n);
        vertices += n;
        count -= n;
    }
    drawn_ = true;
}

// 0x1007b4e58
void cGZDrawContextRenderToSimGL::DrawQuadList(cGZDrawVertex* vertices, int count) {
    while (count > 0) {
        const int n = count < kMaxQuadVertices + 1 ? count : kMaxQuadVertices;
        driver_->InterleavedArrays(kVertexFormat, kVertexStride, vertices);
        driver_->DrawArrays(gd::prim::Quads, 0, n);
        vertices += n;
        count -= n;
    }
    drawn_ = true;
}
