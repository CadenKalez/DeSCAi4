#include "sc4/CatalogItem.h"

namespace sc4 {

cSC4CatalogItem::~cSC4CatalogItem() {
    if (icon_) {
        cIGZUnknown* icon = icon_;
        icon_ = nullptr;
        icon->Release();
    }
}

bool cSC4CatalogItem::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cISC4CatalogItem) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4CatalogItem::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return --refCount_;
}

void cSC4CatalogItem::SetIcon(cIGZUnknown* icon) {
    if (icon) icon->AddRef();
    if (icon_) icon_->Release();
    icon_ = icon;
}

cSC4GlyphTextureInfo::~cSC4GlyphTextureInfo() {
    if (manager_) manager_->Release();
    if (binding_) binding_->Release();
}

void cSC4GlyphTextureInfo::SetManager(IGlyphTextureManager* manager) {
    IGlyphTextureManager* previous = manager_;
    if (previous == manager) return;
    if (manager) manager->AddRef();
    manager_ = manager;
    if (previous) previous->Release();
}

int cSC4GlyphTextureInfo::Release() {
    const int old = refCount_;
    --refCount_;
    if (old != 1) {
        if (old != 2) return old - 1;
        if (manager_) {
            manager_->SignalUnused(this);
            return 0;
        }
    }
    delete this;
    return 0;
}

}  // namespace sc4
