// cRZSysServPtr<T, kServiceId, kInterfaceId> — a framework service looked up by id and held through its
// interface (the same smart-pointer behaviour as cRZAutoRefCount; the lookup goes through a resolver the
// framework installs, because the original reached it through RZGetFramework()). The pointer conversion
// operator of every instantiation is `operator T*()` returning the held interface.
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"
#include "rz/cRZAutoRefCount.h"

using cRZServiceResolver = bool (*)(std::uint32_t serviceId, std::uint32_t interfaceId, void** out);
void SetRZServiceResolver(cRZServiceResolver resolver);
cRZServiceResolver GetRZServiceResolver();

template <class T, std::uint32_t kServiceId, std::uint32_t kInterfaceId>
class cRZSysServPtr {
public:
    cRZSysServPtr() { Acquire(); }
    ~cRZSysServPtr() { Release(); }
    cRZSysServPtr(const cRZSysServPtr&) = delete;
    cRZSysServPtr& operator=(const cRZSysServPtr&) = delete;

    // AddRefs the new service before releasing the old one (cRZSysServPtr<...>::operator=(T*)).
    cRZSysServPtr& operator=(T* service) noexcept {
        if (service) service->AddRef();
        if (service_) service_->Release();
        service_ = service;
        return *this;
    }

    operator T*() const noexcept { return service_; }
    T* operator->() const noexcept { return service_; }
    T* get() const noexcept { return service_; }

private:
    void Acquire() {
        if (cRZServiceResolver resolve = GetRZServiceResolver()) {
            void* out = nullptr;
            if (resolve(kServiceId, kInterfaceId, &out)) service_ = static_cast<T*>(out);
        }
    }
    void Release() {
        if (service_) service_->Release();
        service_ = nullptr;
    }
    T* service_ = nullptr;
};
