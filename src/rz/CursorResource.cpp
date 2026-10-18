#include "rz/CursorResource.h"

static IRZCursorSource* g_cursorSource = nullptr;
IRZCursorSource* RZCursorSource() { return g_cursorSource; }
void SetRZCursorSource(IRZCursorSource* source) { g_cursorSource = source; }

cRZCursorResource::cRZCursorResource() {
    if (g_cursorSource) cursor_ = g_cursorSource->CreateCursor();
}

cRZCursorResource::~cRZCursorResource() {
    if (cursor_) cursor_->Release();
    cursor_ = nullptr;
}

bool cRZCursorResource::QueryInterface(std::uint32_t iid, void** out) {
    if (cursor_ && cursor_->QueryInterface(iid, out)) return true;
    if (iid != kGZIID_cIRZCursorResource) return cGZPersistResource::QueryInterface(iid, out);
    if (out) {
        *out = static_cast<cIRZCursorResource*>(this);
        AddRef();
    }
    return out != nullptr;
}

bool cRZCursorResource::SetCursor(cIGZCursor* cursor) {
    if (cursor) cursor->AddRef();
    if (cursor_) cursor_->Release();
    cursor_ = cursor;
    return true;
}

bool cRZCursorResource::GetCursor(cIGZCursor** out) const {
    return cursor_ ? cursor_->QueryInterface(kGZIID_RZCursor, reinterpret_cast<void**>(out)) : false;
}

bool cRZCursorResource::GetCursorAs(std::uint32_t iid, void** out) const {
    return cursor_ ? cursor_->QueryInterface(iid, out) : false;
}

bool cRZCursorResourceFactory::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIRZCursorResourceFactory) {
        *out = static_cast<cIRZCursorResourceFactory*>(this);
        AddRef();
        return true;
    }
    if (iid == 1 || iid == kGZIID_cIGZPersistResourceFactory) {
        *out = static_cast<cGZPersistResourceFactory*>(this);
        AddRef();
        return true;
    }
    return false;
}

bool cRZCursorResourceFactory::SerialRead(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) {
    if (resource.GetType() != kRZResourceTypeCursor) return false;
    void* raw = nullptr;
    if (!resource.QueryInterface(kGZIID_cIRZCursorResource, &raw)) return false;
    auto* view = static_cast<cIRZCursorResource*>(raw);
    const bool ok = view->LoadFromSerialRecord(&record);
    view->Release();
    return ok;
}

bool cRZCursorResourceFactory::CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) {
    if (type != kRZResourceTypeCursor) return false;
    auto* resource = new cRZCursorResource();
    resource->AddRef();
    const bool ok = resource->QueryInterface(iid, out);
    resource->Release();
    return ok;
}
