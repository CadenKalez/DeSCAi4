// Adapter that lets the sound classes (which use the narrow cIGZPersistRecordService seam)
// talk to the real cGZPersistResourceManager.
#pragma once
#include "gz/cIGZPersistResourceManager.h"

class AudioPersistAdapter final : public cIGZPersistRecordService {
public:
    explicit AudioPersistAdapter(cIGZPersistResourceManager* manager) : manager_(manager) {}

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override { return --refs_; }   // owned by the host

    bool GetResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out, std::uint32_t flags,
                     std::uint32_t owner) override {
        return manager_->GetResource(key, iid, out, flags, reinterpret_cast<cIGZUnknown*>(std::uintptr_t{owner}));
    }
    bool CreateResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out, std::uint32_t flags,
                        std::uint32_t owner) override {
        return manager_->GetNewResource(key, iid, out, flags, reinterpret_cast<cIGZUnknown*>(std::uintptr_t{owner}));
    }
    bool RegisterResource(cIGZUnknown* resource) override {
        void* raw = nullptr;
        if (!resource || !resource->QueryInterface(kGZIID_cIGZPersistResource, &raw)) return false;
        auto* r = static_cast<cIGZPersistResource*>(raw);
        bool ok = manager_->RegisterResource(r);
        r->Release();
        return ok;
    }
    bool RemoveResource(const cGZPersistResourceKey& key) override { return manager_->UnregisterResource(key); }
    bool HasResource(const cGZPersistResourceKey& key) override { return manager_->HasRegisteredResource(key); }
    // The sound classes only test for presence (and release the out parameter), so the segment that
    // holds the key stands in for the record.
    bool FindRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** out) override {
        cIGZPersistDBSegment* segment = nullptr;
        bool ok = manager_->FindDBSegment(key, &segment);
        if (segment) segment->Release();
        if (out) *out = nullptr;
        return ok;
    }
    bool OpenRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** out, std::uint32_t flags) override {
        return manager_->OpenDBRecord(key, out, flags & 2);
    }

private:
    cIGZPersistResourceManager* manager_;
    std::uint32_t refs_ = 1;
};
