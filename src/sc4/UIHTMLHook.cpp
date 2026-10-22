#include "sc4/UIHTMLHook.h"

#include "gz/cIGZPersistDBSegment.h"
#include "rz/RZStringUtils.h"

namespace sc4 {

cSC4UIHTMLHook::cSC4UIHTMLHook(cIGZPersistResourceManager* resourceManager) : pending_(resourceManager) {}

cSC4UIHTMLHook::~cSC4UIHTMLHook() {
    if (resourceManager_) resourceManager_->Release();
}

void cSC4UIHTMLHook::SetResourceManager(cIGZPersistResourceManager* manager) { pending_ = manager; }

bool cSC4UIHTMLHook::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != kGZIID_cISC4UIHTMLHook && iid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

bool cSC4UIHTMLHook::Init() {
    cIGZPersistResourceManager* manager = pending_;
    cIGZPersistResourceManager* old = resourceManager_;
    if (old != manager) {
        if (manager) manager->AddRef();
        resourceManager_ = manager;
        if (old) old->Release();
    }
    return true;
}

bool cSC4UIHTMLHook::Shutdown() {
    if (resourceManager_) {
        cIGZPersistResourceManager* manager = resourceManager_;
        resourceManager_ = nullptr;
        manager->Release();
    }
    return true;
}

bool cSC4UIHTMLHook::TestURL(IHTMLURL& url) {
    return url.GetScheme().IsEqualToChars("sc4", 3, false);
}

bool cSC4UIHTMLHook::KeyFromURL(const std::string& fullUrl, cGZPersistResourceKey& key) {
    cRZString url(fullUrl.c_str());
    if (!url.BeginsWith("sc4://", 6)) return false;
    url.Erase(0, 6);
    cRZString first;
    if (!url.SplitTokenSeparated('/', first)) return false;
    key.type = kHTMLResourceType;
    key.group = 0;
    key.instance = 0;
    if (std::string(url.ToChar()).find('/') == std::string::npos) {
        first.MakeLower();
        const std::string name = first.ToChar();
        if (name == "html") key.group = kHTMLGroupHtml;
        else if (name == "image") key.group = kHTMLGroupImage;
    } else {
        cRZString groupText;
        url.SplitTokenSeparated('/', groupText);
        key.group = RZAtoUint32HexA(groupText.ToChar());
    }
    if (url.Strlen() > 7) key.instance = RZAtoUint32HexA(url.ToChar());
    return true;
}

bool cSC4UIHTMLHook::LoadDocument(IHTMLURL& url, std::string& out) {
    cGZPersistResourceKey key;
    if (!KeyFromURL(url.GetFullURL().ToChar(), key)) return false;
    return LoadDocument(key, out);
}

bool cSC4UIHTMLHook::LoadDocument(const cGZPersistResourceKey& key, std::string& out) {
    cIGZPersistDBSegment* segment = nullptr;
    if (!resourceManager_->FindDBSegment(key, &segment)) return false;
    bool loaded = false;
    std::uint32_t size = 0;
    size = segment->ReadRecord(key, nullptr, size);   // a null buffer asks for the record size
    if (size != 0xffffffffu) {
        out.assign(size, '\0');
        segment->ReadRecord(key, out.data(), size);
        loaded = true;
    }
    segment->Release();
    return loaded;
}

}  // namespace sc4
