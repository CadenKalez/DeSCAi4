#include "sc4/BackingStore.h"

namespace sc4 {

TranslatedBlit ComputeTranslatedBlit(int W, int H, int dx, int dy) {
    TranslatedBlit r;
    r.validW = W;
    r.validH = H;
    unsigned code = dx < 1 ? (dx < 0 ? 1u : 0u) : 2u;
    if (dy < 1) {
        if (dy != 0) code |= 8;
    } else {
        code |= 4;
    }
    auto strip = [](int (&s)[4], int a, int b, int c, int d) { s[0] = a; s[1] = b; s[2] = c; s[3] = d; };
    switch (code) {
    case 1:   // moved left: content shifts right, the left strip is new
        r.srcX = 0; r.srcY = 0; r.width = W + dx; r.height = H; r.dstX = -dx; r.dstY = 0;
        strip(r.stripA, 0, 0, -dx, H);
        break;
    case 2:   // moved right
        r.srcX = dx; r.srcY = 0; r.width = W - dx; r.height = H; r.dstX = 0; r.dstY = 0;
        strip(r.stripA, W - dx, 0, W, H);
        r.validW = W - dx;
        break;
    case 4:   // moved up (dy > 0): content shifts down, the top strip is new
        r.srcX = 0; r.srcY = 0; r.width = W; r.height = H - dy; r.dstX = 0; r.dstY = dy;
        strip(r.stripA, 0, 0, W, dy);
        break;
    case 5:
        r.srcX = 0; r.srcY = 0; r.width = W + dx; r.height = H - dy; r.dstX = -dx; r.dstY = dy;
        strip(r.stripA, 0, 0, -dx, H);
        strip(r.stripB, -dx, 0, W, dy);
        break;
    case 6:
        r.srcX = dx; r.srcY = 0; r.width = W - dx; r.height = H - dy; r.dstX = 0; r.dstY = dy;
        strip(r.stripA, W - dx, 0, W, H);
        strip(r.stripB, 0, 0, W - dx, dy);
        r.validW = W - dx;
        break;
    case 8:
        r.srcX = 0; r.srcY = -dy; r.width = W; r.height = H + dy; r.dstX = 0; r.dstY = 0;
        strip(r.stripA, 0, H + dy, W, H);
        r.validH = H + dy;
        break;
    case 9:
        r.srcX = 0; r.srcY = -dy; r.width = W + dx; r.height = H + dy; r.dstX = -dx; r.dstY = 0;
        strip(r.stripA, 0, 0, -dx, H);
        strip(r.stripB, -dx, H + dy, W, H);
        r.validH = H + dy;
        break;
    case 10:
        r.srcX = dx; r.srcY = -dy; r.width = W - dx; r.height = H + dy; r.dstX = 0; r.dstY = 0;
        strip(r.stripA, W - dx, 0, W, H);
        strip(r.stripB, 0, H + dy, W - dx, H);
        r.validW = W - dx;
        r.validH = H + dy;
        break;
    default: break;   // 0 (no movement), 3 and 7 (impossible): a zero-sized blit
    }
    return r;
}

bool cSC43DBackingStore::CreateBackingStore(gd::cIGZGDriver& driver) {
    std::uint32_t v[4];
    driver.GetViewport(v);   // (x, y, x + w, y + h)
    return store_->Create(v[2] - v[0], v[3] - v[1]);
}

void cSC43DBackingStore::DestroyBackingStore() { store_->Destroy(); }

bool cSC43DBackingStore::SaveBackingStore() {
    if (justZoomed) {
        const int sx = shakeX, sy = shakeY;
        if (sy != 0 || sx != 0) {
            shakeX = shakeY = 0;
            SaveBackingStore(0, 0, viewWidth, viewHeight);
            shakeX = sx;
            shakeY = sy;
            justZoomed = false;
        }
    }
    return SaveBackingStore(0, 0, viewWidth, viewHeight);
}

// Copies the on-screen block (x, y, w, h) into the store. With a screen-shake offset the block is shifted and clipped to the view;
// a block that ends up empty is "done" (true). Without partial-write support only a full-view copy is allowed.
bool cSC43DBackingStore::SaveBackingStore(int x, int y, int w, int h) {
    int srcX = x, dstY = y;
    int srcY = y;
    if (shakeY != 0 || shakeX != 0) {
        int dstX = x - shakeX;
        if (dstX < 0) {
            if (w - shakeX == 0 || w < shakeX) return true;
            srcX = x + shakeX;
            dstX = 0;
            w -= shakeX;
        } else if (viewWidth < dstX + w) {
            w = viewWidth - dstX;
            if (w == 0 || viewWidth < dstX) return true;
        }
        dstY = y - shakeY;
        x = dstX;
        if (dstY < 0) {
            if (h - shakeY == 0 || h < shakeY) return true;
            srcY = shakeY + y;
            h -= shakeY;
            dstY = 0;
        } else if (viewHeight < dstY + h) {
            h = viewHeight - dstY;
            if (h == 0 || viewHeight < dstY) return true;
        }
    }
    if (h < 1) return false;
    if (w > 0) {
        if (!partialWritesOk && (w != viewWidth || h != viewHeight)) return false;
        store_->SaveFromFrame(srcX, srcY, w, h, x, dstY);
        return true;
    }
    return false;
}

void cSC43DBackingStore::RestoreBackingStore() {
    int sx = shakeX, sy = shakeY;
    if (sx == 0 && sy == 0) {
        RestoreBackingStore(0, 0, viewWidth, viewHeight, 0, 0);
        return;
    }
    const int srcX = sx < 0 ? -sx : 0, srcY = sy < 0 ? -sy : 0;
    const int dstX = sx < 0 ? 0 : sx, dstY = sy < 0 ? 0 : sy;
    const int w = viewWidth - (sx < 0 ? -sx : sx), h = viewHeight - (sy < 0 ? -sy : sy);
    RestoreBackingStore(srcX, srcY, w, h, dstX, dstY);
}

void cSC43DBackingStore::RestoreBackingStore(int srcX, int srcY, int w, int h, int dstX, int dstY) {
    store_->RestoreToFrame(srcX, srcY, w, h, dstX, dstY);
}

void cSC43DBackingStore::ApplyTranslatedBlit(const TranslatedBlit& b) {
    store_->RestoreToFrame(b.srcX, b.srcY, b.width, b.height, b.dstX, b.dstY);
}

}  // namespace sc4
