// cGZBufferResource — a persisted bitmap resource (type 0x856ddbac) wrapping a cIGZBuffer (36 functions,
// 0x100614b10..0x1006161xx). Interface cIGZBufferResource (0x22b9da52) at +0x20; the buffer pointer is at +0x28.
// Record formats (all little-endian): 'BM' Windows bitmaps (4/8/24-bit, bottom-up, rows padded to 4 bytes);
// version 0x62b9da24 {version, width, height, u32 pixels}; version 1 {version, size, width, height, colour type, raw
// rows}; version 2 {same header, RefPack-compressed rows}. Saving writes version 2, or (for 16-bit buffers, type
// 0x55d4fb53) a 24-bit bitmap.
// Quirks kept: the 24-to-24 bitmap translation fills the buffer with 0xff instead of copying; the 24-to-32 one indexes
// source and destination with the same (row * stride / 4) byte offset; version 1 copies its rows with the strides
// swapped when the surface stride differs from the row size.
#pragma once
#include <cstdint>
#include <functional>

#include "gz/Buffer.h"
#include "gz/cGZPersistResourceManager.h"
#include "gz/cIGZPersistDBSerialRecord.h"

constexpr std::uint32_t kGZIID_cIGZBufferResource = 0x22b9da52;
constexpr std::uint32_t kGZRecordTypeBitmap24 = 0x55d4fb53;
constexpr std::uint32_t kGZBufferResourceV0 = 0x62b9da24;

class cIGZBufferResource : public cIGZUnknown {
public:
    virtual bool SetBuffer(cIGZBuffer* buffer) = 0;
    virtual bool GetBuffer(cIGZBuffer** out) const = 0;
    virtual bool GetBufferAs(std::uint32_t iid, void** out) const = 0;
    virtual bool SaveToSerialRecord(cIGZPersistDBSerialRecord* record, std::uint32_t format) const = 0;
    virtual bool LoadFromSerialRecord(cIGZPersistDBSerialRecord* record, const cGZBufferColorType& colorType) = 0;
};

// Colour conversion after a raw load (ConvertBufferFromFormatToFormat, not recovered): called with the buffer and the
// colour type that was asked for.
using BufferFormatConverter = std::function<bool(cIGZBuffer* buffer, std::uint32_t colorType)>;
void SetBufferFormatConverter(BufferFormatConverter converter);
// Creates the buffer a resource wraps (COM class 0xc470d325, iid 0x20732180) when the COM is not available.
using BufferMaker = std::function<cIGZBuffer*()>;
void SetBufferMaker(BufferMaker maker);

class cGZBufferResource : public cGZPersistResource, public cIGZBufferResource {
public:
    cGZBufferResource();

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZPersistResource::AddRef(); }
    std::uint32_t Release() override { return cGZPersistResource::Release(); }

    bool DebugInstanceTag(cIGZString&) { return false; }
    bool DebugClassTag(cIGZString&) { return false; }
    bool DebugTypeTag(cIGZString&) { return false; }

    bool SetBuffer(cIGZBuffer* buffer) override;
    bool GetBuffer(cIGZBuffer** out) const override;
    bool GetBufferAs(std::uint32_t iid, void** out) const override;
    bool SaveToSerialRecord(cIGZPersistDBSerialRecord* record, std::uint32_t format) const override;
    bool LoadFromSerialRecord(cIGZPersistDBSerialRecord* record, const cGZBufferColorType& colorType) override;

protected:
    ~cGZBufferResource() override;

private:
    struct Header {                 // tGZBuffResHdr
        std::uint32_t version = 0;
        std::uint32_t size = 0;     // bytes of pixel data that follow
        std::uint32_t width = 0, height = 0;
        std::int32_t colorType = 0;
    };

    bool save_v2(cIGZPersistDBSerialRecord* record) const;
    bool save_bitmap_24(cIGZPersistDBSerialRecord* record) const;
    bool load_bitmap(cIGZPersistDBSerialRecord* record, const cGZBufferColorType& colorType);
    bool load_v0(cIGZPersistDBSerialRecord* record, std::uint32_t width, std::uint32_t height, const cGZBufferColorType& colorType);
    bool load_v1(cIGZPersistDBSerialRecord* record, const Header& header);
    bool load_v2(cIGZPersistDBSerialRecord* record, const Header& header);
    bool translate_bmp(const std::uint8_t* info, const std::uint8_t* pixels, std::size_t pixelBytes);
    void translate_4_to_16(const std::uint8_t* info, const std::uint8_t* pixels);
    void translate_8_to_8(const std::uint8_t* info, const std::uint8_t* pixels);
    void translate_8_to_16(const std::uint8_t* info, const std::uint8_t* pixels);
    void translate_24_to_16(const std::uint8_t* info, const std::uint8_t* pixels);
    void translate_24_to_24(const std::uint8_t* info);
    void translate_24_to_32(const std::uint8_t* info, const std::uint8_t* pixels, std::size_t pixelBytes);

    cIGZBuffer* buffer_ = nullptr;   // +0x28
};
