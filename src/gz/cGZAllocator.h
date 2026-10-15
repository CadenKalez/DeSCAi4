// Allocator interface and its two implementations: cGZDefaultAllocator (0x1002884ee..0x10028871e, iid
// 0x3ae4beaa, a malloc wrapper with its own refcount at +8) and cGZAllocatorService (0x10028856c.., service id
// 0x3ae4bea3, priority 9000000, answers 0x3ae4beab for the allocator view). Also cRZExceptionNotification
// (0x1003faefc, iid 0xa4b30150): the "notify on exception" hook, whose Notify does nothing and returns true.
#pragma once
#include <cstdint>
#include <cstdlib>

#include "gz/cIGZUnknown.h"
#include "rz/cRZSystemService.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZAllocator = 0x3ae4beaa, kGZIID_cIGZAllocatorService = 0x3ae4beab;
constexpr std::uint32_t kGZSID_cGZAllocatorService = 0x3ae4bea3;
constexpr std::int32_t kGZAllocatorServicePriority = 9000000;
constexpr std::uint32_t kGZIID_cIRZExceptionNotification = 0xa4b30150;

class cIGZAllocator : public cIGZUnknown {
public:
    virtual void* Allocate(std::uint32_t size) = 0;                       // [3]
    virtual void Deallocate(void* memory) = 0;                            // [4]
    virtual void* Reallocate(void* memory, std::uint32_t size) = 0;       // [5]
};

class cGZDefaultAllocator final : public cIGZAllocator {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    void* Allocate(std::uint32_t size) override { return std::malloc(size); }
    void Deallocate(void* memory) override { std::free(memory); }
    void* Reallocate(void* memory, std::uint32_t size) override { return std::realloc(memory, size); }

private:
    std::uint32_t refCount_ = 0;     // +8
};

class cGZAllocatorService final : public cIGZAllocator, public cRZSystemService {
public:
    cGZAllocatorService() : cRZSystemService(kGZSID_cGZAllocatorService, kGZAllocatorServicePriority) {}
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }
    std::uint32_t Release() override { return cRZSystemService::Release(); }
    void* Allocate(std::uint32_t size) override { return std::malloc(size); }
    void Deallocate(void* memory) override { std::free(memory); }
    void* Reallocate(void* memory, std::uint32_t size) override { return std::realloc(memory, size); }
};

class cIRZExceptionNotification : public cIGZUnknown {
public:
    virtual bool Notify(const char* message, int code) = 0;     // [3]
};

class cRZExceptionNotification final : public cIRZExceptionNotification {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool Notify(const char*, int) override { return true; }

private:
    std::uint32_t refCount_ = 0;     // +8
};

}  // namespace gz
