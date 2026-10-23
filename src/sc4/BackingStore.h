// The backing-store logic of cSC43DRender (the city view's cached static scene), ported from raw OpenGL framebuffer calls to
// the driver's IBackingStore service: CreateBackingStore (0x1007699ee), SaveBackingStore() / SaveBackingStore(x,y,w,h)
// (0x1007641b4 / 0x100764aec), RestoreBackingStore() / RestoreBackingStore(6 ints) (0x10076605c / 0x100769b1c) and the blit
// geometry of DoTranslatedViewUpdate (0x100763d10). cSC43DRender embeds this; the fields mirror its members.
#pragma once
#include <cstdint>

#include "render/gd/IBackingStore.h"
#include "render/gd/IGZGDriver.h"

namespace sc4 {

struct TranslatedBlit {
    // copy the block (srcX, srcY, w, h) of the store onto the frame at (dstX, dstY)
    int srcX = 0, srcY = 0, width = 0, height = 0, dstX = 0, dstY = 0;
    // the two strips the translation uncovers and that must be redrawn (left, top, right, bottom); empty when all zero
    int stripA[4] = {0, 0, 0, 0}, stripB[4] = {0, 0, 0, 0};
    // cSC43DRender +0x32c..+0x338: destination origin and the valid extent afterwards
    int validX = 0, validY = 0, validW = 0, validH = 0;
};
// DoTranslatedViewUpdate(dx, dy) for a view of the given size: which block moves and which strips are exposed.
TranslatedBlit ComputeTranslatedBlit(int viewWidth, int viewHeight, int dx, int dy);

class cSC43DBackingStore {
public:
    explicit cSC43DBackingStore(gd::IBackingStore* store) : store_(store) {}

    bool CreateBackingStore(gd::cIGZGDriver& driver);           // true when the store is complete
    void DestroyBackingStore();                                  // StaticSetGDriver / StaticShutdown cleanup
    bool HasBackingStore() const { return store_->Exists(); }
    bool SaveBackingStore();                                     // SaveBackingStore()
    bool SaveBackingStore(int x, int y, int w, int h);           // true = done or nothing to do, false = refused
    void RestoreBackingStore();                                  // RestoreBackingStore()
    void RestoreBackingStore(int srcX, int srcY, int w, int h, int dstX, int dstY);
    void ApplyTranslatedBlit(const TranslatedBlit& b);           // the blit step of DoTranslatedViewUpdate

    // cSC43DRender members used by the logic
    int viewWidth = 0, viewHeight = 0;           // +0xb4, +0xb8
    bool partialWritesOk = true;                 // +0xb0 (driver CanDoPartialRegionWrites)
    bool storeValid = false;                     // +0xb1
    int shakeX = 0, shakeY = 0;                  // +0x398, +0x39c (screen shake offsets)
    // g_MacJustZoomed: set by cSC4CameraControl::SetZoom, read and cleared by SaveBackingStore(). Inline so both
    // classes share one flag without the camera control having to link this class (the original has one global).
    static inline bool justZoomed = false;

private:
    gd::IBackingStore* store_;
};

}  // namespace sc4
