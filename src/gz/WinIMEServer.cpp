#include "gz/WinIMEServer.h"

namespace {
IIMELanguageSource* g_language = nullptr;
std::uint32_t CurrentLanguage() { return g_language ? g_language->CurrentLanguage() : 0; }
}  // namespace

void SetIMELanguageSource(IIMELanguageSource* source) { g_language = source; }

cGZIMECompositionStringDataEx::cGZIMECompositionStringDataEx() {
    language = CurrentLanguage();
    caret = 0;
    active = false;
    Set();
}

void cGZIMECompositionStringDataEx::Set() {
    if (textStorage.empty()) {
        text = nullptr;
        attributes = nullptr;
        length = 0;
    } else {
        text = textStorage.data();
        attributes = attributeStorage.data();
        length = static_cast<std::uint32_t>(textStorage.size());
    }
}

cGZIMECandidateListDataEx::cGZIMECandidateListDataEx() {
    language = CurrentLanguage();
    selection = 0;
    active = false;
    Set();
}

void cGZIMECandidateListDataEx::Set() {
    if (strings.empty()) {
        candidates = nullptr;
        count = 0;
        return;
    }
    count = static_cast<std::uint32_t>(strings.size());
    pointers.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        IMEString& s = strings[i];
        if (s.empty() || s.back() != 0) s.push_back(0);   // every candidate must be NUL terminated
        pointers[i] = s.data();
    }
    candidates = pointers.data();
}

cGZWinIMEServer::~cGZWinIMEServer() = default;

bool cGZWinIMEServer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZWinIMEServer) {
        *out = static_cast<cIGZWinIMEServer*>(this);
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

std::uint32_t cGZWinIMEServer::Release() {
    if (refCount_ > 1) return --refCount_;
    Shutdown();
    delete this;
    return 0;
}

bool cGZWinIMEServer::Init() {
    if (!initialized_) {
        initialized_ = true;
        cRZSysServPtr<cIGZMessageServer, kGZSID_cGZMessageServer, kGZIID_cIGZMessageServer> server;
        messageServer_ = server.get();
        cRZSysServPtr<cIGZMessageServer2, 0x04fa845b, 0x652294c7> server2;
        messageServer2_ = server2.get();
        if (enabled_) {
            enabled_ = false;
            SetEnabled(true);
        }
    }
    return true;
}

bool cGZWinIMEServer::Shutdown() {
    if (initialized_) {
        initialized_ = false;
        messageServer_ = nullptr;
        messageServer2_ = nullptr;
        SetEnabled(false);
    }
    return true;
}

bool cGZWinIMEServer::SetCompositionString(const std::uint16_t* text, std::uint32_t length) {
    IMEString value;
    if (text && length != 0) value.assign(text, length);
    composition_.textStorage = value;
    return true;
}

bool cGZWinIMEServer::SetCandidateListSelection(std::uint32_t index) {
    const std::uint32_t size = static_cast<std::uint32_t>(candidates_.strings.size());
    return index <= size && size - index != 0;
}
