// cHitTrackDataBlock — a block of Hit track bytecode plus a map of entry-point keys to byte
// offsets, tagged with the file it came from. Recovered from Sim City 4 Deluxe Editionsub 1.2.1.
//
// Original layout (0x70): +0x00 vptr, +0x08 refcount, +0x10 data pointer, +0x18 unordered_map
// <uint32,uint32>, +0x40 data size, +0x48 cRZString filename.
//
// The data block is *borrowed*: the object never frees or copies it.
#pragma once
#include <cstdint>
#include <unordered_map>

#include "audio/interfaces/cIHitTrackDataBlock.h"
#include "rz/cRZString.h"

class cHitTrackDataBlock final : public cIHitTrackDataBlock {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;  // deletes when the count is <= 1

    const std::uint8_t* DataStartAddress() const override { return data_; }
    std::uint32_t DataSize() const override { return dataSize_; }
    bool GetEntryPointOffset(std::uint32_t entryKey, std::uint32_t& outOffset) override;
    void GetFilename(cIGZString& outFilename) override { outFilename.Copy(filename_); }
    bool SetDataBlock(const std::uint8_t* data, std::uint32_t size) override;
    bool AddEntryPoint(std::uint32_t entryKey, std::uint32_t offset) override;

    // Stores the name upper-cased. A name without a drive is first made absolute against the
    // application directory (`applicationDirectory`; the original asks RZ::ADir()).
    void SetFilename(const cIGZString& filename, const std::string& applicationDirectory);

private:
    virtual ~cHitTrackDataBlock() = default;

    std::int32_t refCount_ = 0;
    const std::uint8_t* data_ = nullptr;
    std::unordered_map<std::uint32_t, std::uint32_t> entryPoints_;
    std::uint32_t dataSize_ = 0;
    cRZString filename_;
};
