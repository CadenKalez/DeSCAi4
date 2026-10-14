// cIHitTrackDataBlock — a loaded block of Hit track bytecode with named entry points.
// Slots 3..8 (byte offsets +0x18..+0x40).
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"

class cIHitTrackDataBlock : public cIGZUnknown {
public:
    virtual const std::uint8_t* DataStartAddress() const = 0;                                 // [3]
    virtual std::uint32_t DataSize() const = 0;                                               // [4]
    // [5] Looks up the byte offset (into the block) of the entry point with this key.
    virtual bool GetEntryPointOffset(std::uint32_t entryKey, std::uint32_t& outOffset) = 0;
    virtual void GetFilename(cIGZString& outFilename) = 0;                                    // [6]
    virtual bool SetDataBlock(const std::uint8_t* data, std::uint32_t size) = 0;              // [7] true
    virtual bool AddEntryPoint(std::uint32_t entryKey, std::uint32_t offset) = 0;             // [8] true
};

constexpr std::uint32_t kGZIID_cIHitTrackDataBlock = 0x9b82eae7;
