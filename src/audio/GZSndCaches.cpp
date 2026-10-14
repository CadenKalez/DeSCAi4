#include "audio/GZSndCaches.h"

#include <algorithm>

#include "audio/AudioServices.h"
#include "gz/cGZAny.h"
#include "gz/cIGZPropertySet.h"

namespace {
cGZPersistResourceKey InstanceKey(std::uint32_t instance) {
    return {cGZSndPropertyCache::kInstanceKeyType, cGZSndPropertyCache::kInstanceKeyGroup, instance};
}
constexpr std::uint32_t kIidPersistResource = 0x456b8f1d;
}  // namespace

// ============================================================================================
// property cache
// ============================================================================================

cGZSndPropertyCache::cGZSndPropertyCache() { persist_ = Audio::PersistService(); }

bool cGZSndPropertyCache::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSndPropertyCache || iid == kGZIID_cIGZUnknown) {
        *out = static_cast<cIGZSndPropertyCache*>(this);
        AddRef();
        return true;
    }
    return false;
}

// cGZSndPropertyCache::Release() @ 0x10047a3d8: decrements first and destroys at zero.
std::uint32_t cGZSndPropertyCache::Release() {
    const std::int32_t count = --refCount_;
    if (count != 0) return static_cast<std::uint32_t>(count);
    delete this;
    return 0;
}

bool cGZSndPropertyCache::AddInstance(std::uint32_t instance) {
    if (!persist_) return false;
    void* raw = nullptr;
    if (!persist_->CreateResource(InstanceKey(instance), kIidPersistResource, &raw, 0, 0) || !raw) return false;
    auto* resource = static_cast<cIGZUnknown*>(raw);
    cRZAutoRefCount<cIGZUnknown> holder;
    holder = resource;
    resource->Release();   // the creation reference
    if (!persist_->RegisterResource(resource)) return false;
    instances_[instance] = holder;
    return true;
}

bool cGZSndPropertyCache::RemoveInstance(std::uint32_t instance) {
    instances_.erase(instance);
    return persist_ ? persist_->RemoveResource(InstanceKey(instance)) : false;
}

bool cGZSndPropertyCache::HasInstance(std::uint32_t instance) {
    return persist_ ? persist_->HasResource(InstanceKey(instance)) : false;
}

namespace {
// Fetches the property set resource of `instance`; the caller releases it.
cIGZPropertySet* OpenPropertySet(cIGZPersistRecordService* persist, std::uint32_t instance) {
    if (!persist) return nullptr;
    void* raw = nullptr;
    if (!persist->GetResource(InstanceKey(instance), kGZIID_cIGZPropertySet, &raw, 0, 0) || !raw) return nullptr;
    return static_cast<cIGZPropertySet*>(raw);
}
}  // namespace

bool cGZSndPropertyCache::SetInstanceProperty(std::uint32_t instance, std::uint32_t property, const cIGZAny& value) {
    cIGZPropertySet* set = OpenPropertySet(persist_.get(), instance);
    if (!set) return false;
    const bool ok = set->SetValue(property, value);
    set->Release();
    return ok;
}

bool cGZSndPropertyCache::GetInstanceProperty(std::uint32_t instance, std::uint32_t property, cIGZAny* value) {
    cIGZPropertySet* set = OpenPropertySet(persist_.get(), instance);
    if (!set) return false;
    const bool ok = set->Value(property, value);
    set->Release();
    return ok;
}

bool cGZSndPropertyCache::HasInstanceProperty(std::uint32_t instance, std::uint32_t property) {
    cIGZPropertySet* set = OpenPropertySet(persist_.get(), instance);
    if (!set) return false;
    const bool ok = set->HasValue(property);
    set->Release();
    return ok;
}

bool cGZSndPropertyCache::RemoveInstanceProperty(std::uint32_t instance, std::uint32_t property) {
    cIGZPropertySet* set = OpenPropertySet(persist_.get(), instance);
    if (!set) return false;
    const bool ok = set->ClearValue(property);
    set->Release();
    return ok;
}

bool cGZSndPropertyCache::SetGlobalProperty(std::uint32_t property, const cIGZAny& value) {
    const bool result = SetInstanceProperty(kGlobalInstance, property, value);
    PostPropertyCacheMessage(kEventGlobalPropertySet, property, 0, 0, 0);
    return result;
}

bool cGZSndPropertyCache::GetGlobalProperty(std::uint32_t property, cIGZAny* value) {
    return GetInstanceProperty(kGlobalInstance, property, value);
}

bool cGZSndPropertyCache::HasGlobalProperty(std::uint32_t property) {
    return HasInstanceProperty(kGlobalInstance, property);
}

bool cGZSndPropertyCache::RemoveGlobalProperty(std::uint32_t property) {
    RemoveInstanceProperty(kGlobalInstance, property);
    PostPropertyCacheMessage(kEventGlobalPropertyRemoved, property, 0, 0, 0);
    return true;
}

bool cGZSndPropertyCache::SetInstancePropertyUint32(std::uint32_t instance, std::uint32_t property, std::uint32_t value) {
    return SetInstanceProperty(instance, property, AnyUint32(value));
}

bool cGZSndPropertyCache::SetGlobalPropertyUint32(std::uint32_t property, std::uint32_t value) {
    return SetGlobalProperty(property, AnyUint32(value));
}

void cGZSndPropertyCache::PostPropertyCacheMessage(std::uint32_t id, std::uint32_t d1, std::uint32_t d2,
                                                   std::uint32_t d3, std::uint32_t d4) {
    if (cIGZSndSys* system = Audio::SoundSystem()) system->PostEventIds(id, d1, d2, d3, d4);
}

// ============================================================================================
// instance cache
// ============================================================================================

bool cGZSndInstanceCache::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZSndInstanceCache) {
        *out = static_cast<cIGZSndInstanceCache*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZSndInstanceCache::Release() {
    if (refCount_ == 1) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

bool cGZSndInstanceCache::AddObject(std::uint32_t object) {
    if (!HasObject(object)) objects_[object] = std::make_unique<std::list<std::uint32_t>>();
    return true;
}

bool cGZSndInstanceCache::RemoveObject(std::uint32_t object) {
    objects_.erase(object);
    return true;
}

bool cGZSndInstanceCache::HasObject(std::uint32_t object) { return objects_.count(object) != 0; }

std::list<std::uint32_t>* cGZSndInstanceCache::GetObjectInstanceIdList(std::uint32_t object) {
    const auto it = objects_.find(object);
    return it == objects_.end() ? nullptr : it->second.get();
}

bool cGZSndInstanceCache::AddInstance(std::uint32_t object, std::uint32_t instance) {
    std::list<std::uint32_t>* list = GetObjectInstanceIdList(object);
    if (!list) {
        AddObject(object);
        list = GetObjectInstanceIdList(object);
    }
    if (std::find(list->begin(), list->end(), instance) == list->end()) list->push_back(instance);
    return true;
}

// @ 0x100478f08: removes the first instance of the object (not the one named).
bool cGZSndInstanceCache::RemoveInstance(std::uint32_t object, std::uint32_t) {
    std::list<std::uint32_t>* list = GetObjectInstanceIdList(object);
    if (!list) return false;
    if (!list->empty()) list->pop_front();
    return true;
}

// @ 0x100478f7e: true when the object has any instance.
bool cGZSndInstanceCache::HasInstance(std::uint32_t object, std::uint32_t) {
    std::list<std::uint32_t>* list = GetObjectInstanceIdList(object);
    return list != nullptr && !list->empty();
}

bool cGZSndInstanceCache::RemoveAllInstances(std::uint32_t) { return true; }

bool cGZSndInstanceCache::FindFirstObjectFromInstance(std::uint32_t instance, std::uint32_t& object) {
    for (auto& entry : objects_) {
        const auto& ids = *entry.second;
        if (std::find(ids.begin(), ids.end(), instance) != ids.end()) {
            object = entry.first;
            return true;
        }
    }
    return false;
}
