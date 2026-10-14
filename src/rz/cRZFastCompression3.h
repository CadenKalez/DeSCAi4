// cRZFastCompression3 — RefPack (QFS) compressor/decompressor used by DBPF records.
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100aa0c50, 17 functions).
//
// Stream layout produced by CompressData: u32 total length (LE, includes itself), then the RefPack
// stream: 0x10 0xFB, 24-bit big-endian uncompressed size, commands, terminated by a 0xFC..0xFF byte.
// The encoder is a hash-chain matcher (64K heads, 128K-entry chain, 128 KiB window, matches up to
// 1028 bytes). Deviations from the original:
//  * hash/chain tables hold 32-bit positions instead of 64-bit ones (halves the scratch RAM,
//    identical output for inputs below 2 GiB);
//  * the decoder bounds-checks both the source and the destination (the original trusts them).
#pragma once
#include <cstdint>

#include "gz/cIGZCompression.h"

class cRZFastCompression3 final : public cIGZCompression {
public:
    cRZFastCompression3() = default;
    ~cRZFastCompression3() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    bool CompressData(const void* source, std::uint32_t length, std::uint8_t* destination,
                      std::uint32_t& outLength) override;
    bool DecompressData(const std::uint8_t* source, std::uint32_t length, void* destination,
                        std::uint32_t& outLength) override;
    std::uint32_t GetMaxLengthRequiredForCompressedData(std::uint32_t length) override {
        return length * 15u / 10u + 0x400u;
    }
    std::uint32_t GetLengthOfCompressedData(const std::uint8_t* data) override;
    std::uint32_t GetLengthOfDecompressedData(const std::uint8_t* data) override;
    void GetCompressionVersionString(cIGZString& out) override;

    // Validates the 4-byte prefix + 0x10FB / 0x11FB signature and the 16 MiB size limits.
    bool ValidateCompressedData(const std::uint8_t* data, std::uint32_t length);
    // Quick mode only indexes the first position of every match (faster, slightly larger output).
    void EnableQuickCompression(bool enable) { quick_ = enable; }

private:
    std::uint32_t refCount_ = 0;
    bool quick_ = false;
};
