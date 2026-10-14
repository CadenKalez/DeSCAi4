// cIGZCompression — compression service interface (slots 3..8 recovered from the vtable of
// cRZFastCompression3 @ 0x100aa0c50; the destructors are declared by the concrete class).
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"

class cIGZCompression : public cIGZUnknown {
public:
    // [3] Output starts with a 4-byte little-endian total length; `outLength` receives it too.
    virtual bool CompressData(const void* source, std::uint32_t length, std::uint8_t* destination,
                              std::uint32_t& outLength) = 0;
    // [4] `source` includes the 4-byte length prefix; `outLength` is the destination capacity on
    // entry and the decompressed size on exit.
    virtual bool DecompressData(const std::uint8_t* source, std::uint32_t length, void* destination,
                                std::uint32_t& outLength) = 0;
    virtual std::uint32_t GetMaxLengthRequiredForCompressedData(std::uint32_t length) = 0;  // [5]
    virtual std::uint32_t GetLengthOfCompressedData(const std::uint8_t* data) = 0;          // [6]
    virtual std::uint32_t GetLengthOfDecompressedData(const std::uint8_t* data) = 0;        // [7]
    virtual void GetCompressionVersionString(cIGZString& out) = 0;                          // [8]
};

constexpr std::uint32_t kGZIID_cIGZCompression = 0x8359908e;
