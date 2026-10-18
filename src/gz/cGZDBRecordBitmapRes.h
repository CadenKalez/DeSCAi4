// cGZDBRecordBitmapRes — a read-only database record that serves a bitmap stored as a Windows resource (vtable
// 0x100ad6540; 11 functions at 0x100616112..0x100616b2a). On the Mac port Init has no resource table to read and
// always fails, so the record stays empty; the accessors still follow the original: reading needs a buffer and enough
// bytes left, writing, resizing and seeking all fail, SkipBytes moves the read position without a bound check.
// Layout: +0x60 buffer (array new), +0x68 size, +0x6c read position.
#pragma once
#include <cstdint>
#include <memory>

#include "gz/cGZPersistDBSerialRecord.h"

class cGZDBRecordBitmapRes final : public cGZPersistDBSerialRecord {
public:
    explicit cGZDBRecordBitmapRes(cIGZPersistDBSegment* segment = nullptr, bool littleEndian = true)
        : cGZPersistDBSerialRecord(segment, littleEndian) {}
    ~cGZDBRecordBitmapRes() override { data_.reset(); }   // @ 0x100616112

    bool Init(void* instance, std::uint32_t resourceId);                       // @ 0x10061620c: always false
    std::uint32_t GetSize() override { return data_ ? size_ : 0; }              // @ 0x100616214
    bool SetSize(std::uint32_t) override { return false; }                      // @ 0x100616b14
    bool GetSize(std::uint32_t& out) { out = GetSize(); return true; }          // @ 0x100616b2a
    std::uint32_t GetPosition() override { return position_; }                  // @ 0x10061623c
    bool GetFieldVoid(void* destination, std::uint32_t byteCount) override;    // @ 0x100616250
    bool SetFieldVoid(const void*, std::uint32_t) override { return false; }    // @ 0x1006162d4
    bool SkipBytes(std::uint32_t byteCount);                                    // @ 0x1006162e4
    bool SeekAbsolute(std::uint32_t) override { return false; }                 // the base class stub (0x10038de30)
    bool SeekRelative(std::int32_t) override { return false; }                  // the base class stub (0x10038de40)
    void DoPostClose() override {}

    // Test hook: what a successful Init would have loaded.
    void SetData(std::unique_ptr<std::uint8_t[]> data, std::uint32_t size) { data_ = std::move(data); size_ = size; position_ = 0; }

private:
    std::unique_ptr<std::uint8_t[]> data_;   // +0x60
    std::uint32_t size_ = 0;                 // +0x68
    std::uint32_t position_ = 0;             // +0x6c
};
