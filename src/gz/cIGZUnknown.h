// cIGZUnknown — COM-style base interface of the GZ framework.
// Vtable slots recovered from cRZString's vtable (0x100a67880): [0] QueryInterface,
// [1] AddRef, [2] Release. (Slots [3]/[4] are the complete/deleting destructors.)
#pragma once
#include <cstdint>

class cIGZUnknown {
public:
    virtual bool QueryInterface(std::uint32_t iid, void** out) = 0;
    virtual std::uint32_t AddRef() = 0;
    virtual std::uint32_t Release() = 0;

protected:
    // Deliberately non-virtual: in the original the (complete/deleting) destructors are declared by
    // the concrete classes, so they occupy the slots right after the interface's methods.
    ~cIGZUnknown() = default;
};

// Interface ids (GZIID_*) seen so far.
constexpr std::uint32_t kGZIID_cIGZUnknown = 0x00000001;
constexpr std::uint32_t kGZIID_cIGZString = 0x089b7dc8;
