#include "sc4/UIResourceObjects.h"

namespace sc4 {

cSC4CachedStringTexture::~cSC4CachedStringTexture() {
    if (binding_) binding_->Release();
    if (manager_) manager_->Release();
}

void cSC4CachedStringTexture::SetManager(IGlyphTextureCache* manager) {
    IGlyphTextureCache* previous = manager_;
    if (previous == manager) return;
    if (manager) manager->AddRef();
    manager_ = manager;
    if (previous) previous->Release();
}

int cSC4CachedStringTexture::Release() {
    const int before = refCount_;
    int after = before - 1;
    refCount_ = after;
    if (before != 1 && (before != 2 || (after = 0, manager_ != nullptr))) return after;
    delete this;
    return 0;
}

bool cSC4ScriptServiceResourceFactory::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cISC4ScriptServiceResourceFactory) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4ScriptServiceResourceFactory::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

cIGZUnknown* cSC4ScriptServiceResourceFactory::GZUIScriptLoadBuffer(const cGZPersistResourceKey& key) {
    cIGZUnknown* buffer = host_->LoadScriptBuffer(key);
    if (buffer) buffer->AddRef();
    return buffer;
}

cRZString* cSC4ScriptServiceResourceFactory::GZUIScriptLoadString(const cGZPersistResourceKey& key) {
    auto* text = new cRZString();
    text->AddRef();
    if (!host_->LoadString(key.group, key.instance, *text)) {
        text->Release();
        return nullptr;
    }
    return text;
}

void cSC4ScriptServiceResourceFactory::GZUIScriptLoadString(const cGZPersistResourceKey& key, cIGZString& out) {
    host_->LoadString(key.group, key.instance, out);
}

}  // namespace sc4
