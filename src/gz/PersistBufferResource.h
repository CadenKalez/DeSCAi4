// cGZPersistBufferResource — the older persisted bitmap resource (resource type 0x856ddbac, 34 functions at
// 0x100611e0c..0x10061483e of which seven are non-virtual thunks). It wraps a cIGZBuffer and loads Windows bitmaps (4, 8
// and 24 bit, RLE8) and Targa images (24/32/16 bit, raw or RLE) from files and serial records, converting them to the
// buffer's pixel format; 24 bit images are dithered when a 16 bit ARGB4444 buffer was requested.
//
// Layout of the original (0x58 bytes): +0 cGZPersistResource, +0x20 secondary interface cIGZPersistBufferResource (iid
// 0x06f1568e), +0x28 the buffer, +0x30 the buffer's aggregation interface (iid 0x12f), +0x38 the requested colour type
// ({1, 0} = "display format"), +0x40 vector of dither error cells.
//
// The buffer comes from a class (0xc470d326) that supports COM aggregation: the resource becomes its outer object, so
// QueryInterface for an unknown interface is answered by the buffer.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/Buffer.h"
#include "gz/cGZPersistResourceManager.h"
#include "gz/cIGZPersistDBSerialRecord.h"
#include "rz/cRZString.h"

constexpr std::uint32_t kGZIID_cIGZPersistBufferResource = 0x06f1568e;
constexpr std::uint32_t kGZIID_cIGZBufferAggregate = 0x0000012f;
constexpr std::uint32_t kGZResourceTypeBufferResource = 0x856ddbac;   // (also kGZResourceTypeBuffer in gz/GimexFactory.h)

class cIGZPersistBufferResource : public cIGZUnknown {
public:
    virtual bool GetBuffer(std::uint32_t iid, void** out) = 0;   // [3] QueryInterface on the wrapped buffer
    virtual cIGZBuffer* GetBuffer() const = 0;                   // [4] the buffer with an extra reference (null when none)
    virtual void SetBuffer(cIGZBuffer* buffer) = 0;              // [5]
};

// The aggregation interface of the buffer object (iid 0x12f).
class cIGZBufferAggregate : public cIGZUnknown {
public:
    virtual bool QueryInnerInterface(std::uint32_t iid, void** out) = 0;   // [3] vslot 0x18
    virtual void SetOuter(cIGZUnknown* outer) = 0;                          // [4] vslot 0x20
};

// What the resource and its factory reach outside themselves.
class IPersistBufferHost {
public:
    virtual ~IPersistBufferHost() = default;
    // GZCOM() vslot 0x18 with clsid 0xc470d326 / iid 0x20732180 (counted reference; null when the class is not registered)
    virtual cIGZBuffer* CreateAggregatableBuffer() = 0;
    // GZCOM() vslot 0x18 with clsid 0xc470d325 / iid 0x20732180
    virtual cIGZBuffer* CreateBuffer() = 0;
    // RZ::GS() vslot 0x20: the graphic system's buffer factory (counted reference)
    virtual cIGZBuffer* CreateGraphicSystemBuffer() = 0;
    // ConvertBufferFromFormatToFormat @ 0x1002b36cc: converts a loaded raw buffer to the colour type that was asked for
    virtual bool ConvertBufferFromFormatToFormat(cIGZBuffer* buffer, std::int32_t colorType) = 0;
};

class cGZPersistBufferResource : public cGZPersistResource, public cIGZPersistBufferResource {
public:
    cGZPersistBufferResource(IPersistBufferHost* host, cGZBufferColorType colorType);   // 0x100612d40 (0x100611e0c forwards)

    // cIGZUnknown
    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x100612ef6 (+ thunk 0x100612f7e)
    std::uint32_t AddRef() override { return cGZPersistResource::AddRef(); }       // 0x100611f86 (+ thunk 0x100612f8c)
    std::uint32_t Release() override { return cGZPersistResource::Release(); }     // 0x100612f9a (+ thunk 0x100612fa4)

    // cIGZPersistBufferResource
    bool GetBuffer(std::uint32_t iid, void** out) override;        // 0x1006147ae (+ thunk 0x1006147c8)
    cIGZBuffer* GetBuffer() const override;                        // 0x100611fe8 (+ thunk 0x1006147e2)
    void SetBuffer(cIGZBuffer* buffer) override;                   // 0x10061480a (+ thunk 0x10061483e)

    bool DebugInstanceTag(cIGZString&) { return false; }           // 0x100612fb2 (cGZPersistResource's: false)
    bool DebugClassTag(cIGZString& out);                           // 0x100612fbc: "cSC3Buffer : "
    bool DebugTypeTag(cIGZString&) { return false; }               // 0x100613052

    bool LoadFromRecord(cIGZPersistDBSerialRecord* record, bool = false);   // 0x100611f90: bitmap first, then Targa
    bool LoadBMPFromRecord(cIGZPersistDBSerialRecord* record);              // 0x100613e64
    bool LoadTGAFromRecord(cIGZPersistDBSerialRecord* record);              // 0x100614238
    bool LoadFromFile(const cIGZString& path, bool = false, cGZBufferColorType colorType = {}, bool = false);   // 0x10061216c (.bmp only)

    cGZBufferColorType RequestedColorType() const { return colorType_; }
    const std::vector<std::uint32_t>& DitherCells() const { return errors_; }

protected:
    ~cGZPersistBufferResource() override;   // 0x100612e24 / 0x100612ec8 (0x100612010 forwards)

private:
    // A BITMAPINFOHEADER (0x28 bytes) followed by the 256 palette entries (blue, green, red, unused).
    struct BitmapInfo {
        std::uint8_t bytes[0x428] = {};
        std::int32_t width() const;
        std::int32_t height() const;
        std::uint16_t bitCount() const;
        std::uint32_t compression() const;
        std::uint32_t sizeImage() const;
        std::uint32_t colorsUsed() const;
        const std::uint8_t* palette() const { return bytes + 0x28; }
    };

    // translate_*: BMP pixel rows (bottom-up, padded to 4 bytes) -> the buffer (top-down)
    bool translate_4_to_16(const BitmapInfo& info, const std::uint8_t* pixels);            // 0x100613066
    bool translate_8_to_32(const BitmapInfo& info, const std::uint8_t* pixels);            // 0x1006131a6
    bool translate_8_to_16(const BitmapInfo& info, const std::uint8_t* pixels);            // 0x10061335a
    bool translate_8_to_8(const BitmapInfo& info, const std::uint8_t* pixels);             // 0x10061350e
    bool translate_24_to_16(const BitmapInfo& info, const std::uint8_t* pixels);           // 0x1006135b4
    bool translate_24_to_ARGB4_dither(const BitmapInfo& info, const std::uint8_t* pixels); // 0x1006136a6
    bool translate_ARGB8_to_ARGB4_dither(int width, int height, const std::uint8_t* pixels, int pitch);   // 0x100613924
    bool memcpy_24_to_24(const BitmapInfo& info, const std::uint8_t* pixels);              // 0x100613bae
    bool translate_24_to_32(const BitmapInfo& info, const std::uint8_t* pixels);           // 0x100613c8a
    bool bmp_translate(const BitmapInfo& info, const std::uint8_t* pixels);                // 0x100613d50

    IPersistBufferHost* host_;
    cIGZBuffer* buffer_ = nullptr;                // +0x28
    cIGZBufferAggregate* aggregate_ = nullptr;    // +0x30
    cGZBufferColorType colorType_;                // +0x38
    std::vector<std::uint32_t> errors_;           // +0x40
};
