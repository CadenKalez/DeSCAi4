// cGZAlphaBlt — the alpha-blending blitter service (72 functions, 0x1003953ee..0x100395806). The service object
// itself (id 0x44136fdc, priority -1000000) is a thin shell: every blit forwards to the framework's global blit
// functions of the same name (the real pixel code), and the shadow / bright variants are ShadeBlt with the
// built-in 65536-entry 16-bit colour lookup tables _defaultShadowTable / _defaultBrightTable (built at run time
// by InitAlphaSystem). The global functions are reached through IAlphaBltBackend.
// Layout: +0 cRZSystemService (vtable carries the blit methods), +0x28 the cIGZAlphaBlt interface view.
#pragma once
#include <cstdint>

#include "gz/StreamOps.h"
#include "rz/cRZPoint.h"
#include "rz/cRZSystemService.h"

class cIGZBuffer;
struct cGZBufferColorType;

constexpr std::uint32_t kGZIID_cIGZAlphaBlt = 0xe3260321;
constexpr std::uint32_t kGZSID_cGZAlphaBlt = 0x44136fdc;
constexpr std::int32_t kGZAlphaBltPriority = -1000000;
constexpr std::uint32_t kGZAlphaLevelCount = 0x20;

// The global blit functions.
class IAlphaBltBackend {
public:
    virtual ~IAlphaBltBackend() = default;
    virtual void GenericBufferClip(cIGZBuffer* buffer, cRZRect& source, cRZRect& dest, const cRZRect* clip) = 0;
    virtual void InitAlphaSystem(const cGZBufferColorType& colorType) = 0;
    virtual cIGZBuffer* AlphaSurfaceFromFile(const char* path, std::uint32_t flags) = 0;
    virtual cIGZBuffer* MaskSurfaceFromFile(const char* path) = 0;
    virtual void AlphaBlt(cIGZBuffer* dst, cIGZBuffer* src, cIGZBuffer* alpha, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) = 0;
    virtual void AlphaBltNoClip(cIGZBuffer* dst, cIGZBuffer* src, cIGZBuffer* alpha, const cRZRect& rect, const cRZPoint& at) = 0;
    virtual void AlphaBltConstAlpha(cIGZBuffer* dst, cIGZBuffer* src, float alpha, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) = 0;
    virtual void AlphaBltConstAlphaNoClip(cIGZBuffer* dst, cIGZBuffer* src, float alpha, const cRZRect& rect, const cRZPoint& at) = 0;
    virtual void AlphaBltConstSource(std::uint32_t color, cIGZBuffer* dst, cIGZBuffer* alpha, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) = 0;
    virtual void AlphaBltConstSourceNoClip(std::uint32_t color, cIGZBuffer* dst, cIGZBuffer* alpha, const cRZRect& rect, const cRZPoint& at) = 0;
    virtual void ShadeBltConstAlpha(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at, const std::uint16_t* table, const cRZRect* clip) = 0;
    virtual void ShadeBltConstAlphaNoClip(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at, const std::uint16_t* table) = 0;
    virtual void ShadeBlt(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) = 0;
    virtual void ShadeBltNoClip(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at) = 0;
    virtual void ShadeBltStretch(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& destRect, const cRZRect& srcRect, const cRZRect* clip) = 0;
    virtual void ShadeBltStretchNoClip(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& destRect, const cRZRect& srcRect) = 0;
    virtual void ShadeBltWithAdditionalAlpha(cIGZBuffer* dst, cIGZBuffer* src, float alpha, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) = 0;
    virtual void ShadeBltWithAdditionalAlphaNoClip(cIGZBuffer* dst, cIGZBuffer* src, float alpha, const cRZRect& rect, const cRZPoint& at) = 0;
    virtual void ShadeBltStretchWithAdditionalAlpha(cIGZBuffer* dst, cIGZBuffer* src, float alpha, const cRZRect& destRect, const cRZRect& srcRect, const cRZRect* clip) = 0;
    virtual void ShadeBltStretchWithAdditionalAlphaNoClip(cIGZBuffer* dst, cIGZBuffer* src, float alpha, const cRZRect& destRect, const cRZRect& srcRect) = 0;
    virtual void AlphaBltSelfAlpha(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) = 0;
    virtual void AlphaBltSelfAlphaNoClip(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at) = 0;
    virtual void AlphaBltHalfAlphaVariableSource1ConstSource2(cIGZBuffer* dst, cIGZBuffer* src, std::uint32_t color, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) = 0;
    virtual void AlphaBltHalfAlphaVariableSource1ConstSource2NoClip(cIGZBuffer* dst, cIGZBuffer* src, std::uint32_t color, const cRZRect& rect, const cRZPoint& at) = 0;
    virtual const std::uint16_t* DefaultShadowTable() = 0;
    virtual const std::uint16_t* DefaultBrightTable() = 0;
};

// The cIGZAlphaBlt interface a client holds.
class cIGZAlphaBlt : public cIGZUnknown {
public:
    virtual std::uint32_t GetAlphaLevelCount() const = 0;
};

class cGZAlphaBlt : public cRZSystemService, public cIGZAlphaBlt {
public:
    explicit cGZAlphaBlt(IAlphaBltBackend& backend) : cRZSystemService(kGZSID_cGZAlphaBlt, kGZAlphaBltPriority), backend_(backend) {}
    static cGZAlphaBlt* CreateObject(IAlphaBltBackend& backend) { return new cGZAlphaBlt(backend); }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }
    std::uint32_t Release() override { return cRZSystemService::Release(); }

    void GenericBufferClip(cIGZBuffer* buffer, cRZRect& source, cRZRect& dest, const cRZRect* clip) { backend_.GenericBufferClip(buffer, source, dest, clip); }
    void InitAlphaSystem(const cGZBufferColorType& colorType) { backend_.InitAlphaSystem(colorType); }
    std::uint32_t GetAlphaLevelCount() const override { return kGZAlphaLevelCount; }
    cIGZBuffer* AlphaSurfaceFromFile(const char* path, std::uint32_t flags) { return backend_.AlphaSurfaceFromFile(path, flags); }
    cIGZBuffer* MaskSurfaceFromFile(const char* path) { return backend_.MaskSurfaceFromFile(path); }

    void AlphaBlt(cIGZBuffer* dst, cIGZBuffer* src, cIGZBuffer* alpha, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) { backend_.AlphaBlt(dst, src, alpha, rect, at, clip); }
    void AlphaBltNoClip(cIGZBuffer* dst, cIGZBuffer* src, cIGZBuffer* alpha, const cRZRect& rect, const cRZPoint& at) { backend_.AlphaBltNoClip(dst, src, alpha, rect, at); }
    void AlphaBltConstAlpha(cIGZBuffer* dst, cIGZBuffer* src, float alpha, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) { backend_.AlphaBltConstAlpha(dst, src, alpha, rect, at, clip); }
    void AlphaBltConstAlphaNoClip(cIGZBuffer* dst, cIGZBuffer* src, float alpha, const cRZRect& rect, const cRZPoint& at) { backend_.AlphaBltConstAlphaNoClip(dst, src, alpha, rect, at); }
    void AlphaBltConstSource(std::uint32_t color, cIGZBuffer* dst, cIGZBuffer* alpha, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) { backend_.AlphaBltConstSource(color, dst, alpha, rect, at, clip); }
    void AlphaBltConstSourceNoClip(std::uint32_t color, cIGZBuffer* dst, cIGZBuffer* alpha, const cRZRect& rect, const cRZPoint& at) { backend_.AlphaBltConstSourceNoClip(color, dst, alpha, rect, at); }
    void ShadeBltConstAlpha(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at, const std::uint16_t* table, const cRZRect* clip) { backend_.ShadeBltConstAlpha(dst, src, rect, at, table, clip); }
    void ShadeBltConstAlphaNoClip(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at, const std::uint16_t* table) { backend_.ShadeBltConstAlphaNoClip(dst, src, rect, at, table); }
    void ShadowBlitConstAlpha(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) { backend_.ShadeBltConstAlpha(dst, src, rect, at, backend_.DefaultShadowTable(), clip); }
    void ShadowBlitConstAlphaNoClip(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at) { backend_.ShadeBltConstAlphaNoClip(dst, src, rect, at, backend_.DefaultShadowTable()); }
    void BrightBlitConstAlpha(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) { backend_.ShadeBltConstAlpha(dst, src, rect, at, backend_.DefaultBrightTable(), clip); }
    void BrightBlitConstAlphaNoClip(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at) { backend_.ShadeBltConstAlphaNoClip(dst, src, rect, at, backend_.DefaultBrightTable()); }
    void ShadeBlt(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) { backend_.ShadeBlt(dst, src, rect, at, clip); }
    void ShadeBltNoClip(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at) { backend_.ShadeBltNoClip(dst, src, rect, at); }
    void ShadeBltStretch(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& destRect, const cRZRect& srcRect, const cRZRect* clip) { backend_.ShadeBltStretch(dst, src, destRect, srcRect, clip); }
    void ShadeBltStretchNoClip(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& destRect, const cRZRect& srcRect) { backend_.ShadeBltStretchNoClip(dst, src, destRect, srcRect); }
    void ShadeBltWithAdditionalAlpha(cIGZBuffer* dst, cIGZBuffer* src, float alpha, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) { backend_.ShadeBltWithAdditionalAlpha(dst, src, alpha, rect, at, clip); }
    void ShadeBltWithAdditionalAlphaNoClip(cIGZBuffer* dst, cIGZBuffer* src, float alpha, const cRZRect& rect, const cRZPoint& at) { backend_.ShadeBltWithAdditionalAlphaNoClip(dst, src, alpha, rect, at); }
    void ShadeBltStretchWithAdditionalAlpha(cIGZBuffer* dst, cIGZBuffer* src, float alpha, const cRZRect& destRect, const cRZRect& srcRect, const cRZRect* clip) { backend_.ShadeBltStretchWithAdditionalAlpha(dst, src, alpha, destRect, srcRect, clip); }
    void ShadeBltStretchWithAdditionalAlphaNoClip(cIGZBuffer* dst, cIGZBuffer* src, float alpha, const cRZRect& destRect, const cRZRect& srcRect) { backend_.ShadeBltStretchWithAdditionalAlphaNoClip(dst, src, alpha, destRect, srcRect); }
    void AlphaBltSelfAlpha(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) { backend_.AlphaBltSelfAlpha(dst, src, rect, at, clip); }
    void AlphaBltSelfAlphaNoClip(cIGZBuffer* dst, cIGZBuffer* src, const cRZRect& rect, const cRZPoint& at) { backend_.AlphaBltSelfAlphaNoClip(dst, src, rect, at); }
    void AlphaBltHalfAlphaVariableSource1ConstSource2(cIGZBuffer* dst, cIGZBuffer* src, std::uint32_t color, const cRZRect& rect, const cRZPoint& at, const cRZRect* clip) { backend_.AlphaBltHalfAlphaVariableSource1ConstSource2(dst, src, color, rect, at, clip); }
    void AlphaBltHalfAlphaVariableSource1ConstSource2NoClip(cIGZBuffer* dst, cIGZBuffer* src, std::uint32_t color, const cRZRect& rect, const cRZPoint& at) { backend_.AlphaBltHalfAlphaVariableSource1ConstSource2NoClip(dst, src, color, rect, at); }

private:
    IAlphaBltBackend& backend_;
};
