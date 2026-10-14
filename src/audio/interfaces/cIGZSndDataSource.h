// cIGZSndDataSource — a source of sound data (segment of a packed database: wav, EA XA ADPCM,
// UTalk). Slots 5..22 (+0x28..+0xb0) recovered from cGZSndSegmentWavDataSource (vtable
// 0x100ab4400) and its siblings; slots 3 and 4 are the virtual destructors. Interface id
// 0x3afd825f is accepted by every implementation.
#pragma once
#include <cstdint>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZString.h"

// WAVEFORMATEX-like descriptor (20 bytes in the original objects).
struct GZSndWaveFormat {
    std::uint16_t formatTag = 0;
    std::uint16_t channels = 0;
    std::uint32_t samplesPerSecond = 0;
    std::uint32_t averageBytesPerSecond = 0;
    std::uint16_t blockAlign = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint32_t extra = 0;     // cbSize and padding
};
static_assert(sizeof(GZSndWaveFormat) == 20);

class cIGZSndDataSource : public cIGZUnknown {
public:
    virtual ~cIGZSndDataSource() = default;                                   // [3],[4]
    virtual bool SetPath(const char* path) = 0;                               // [5]  +0x28
    virtual bool GetPath(cIGZString& path) = 0;                               // [6]  +0x30
    virtual bool SetKey(const cGZPersistResourceKey& key) = 0;                // [7]  +0x38
    virtual bool GetKey(cGZPersistResourceKey& key) = 0;                      // [8]  +0x40
    virtual bool Open() = 0;                                                  // [9]  +0x48
    virtual bool Close() = 0;                                                 // [10] +0x50
    virtual bool IsOpen() const = 0;                                          // [11] +0x58
    virtual std::uint32_t GetDataSize() const = 0;                            // [12] +0x60
    virtual bool Lock(std::uint8_t** data, std::uint32_t* size) = 0;          // [13] +0x68
    virtual bool Unlock() = 0;                                                // [14] +0x70
    virtual bool IsLocked() const = 0;                                        // [15] +0x78
    virtual std::int32_t ReadPart(std::uint8_t* destination, std::int32_t byteCount) = 0;   // [16] +0x80
    virtual void ReadReset() = 0;                                             // [17] +0x88
    virtual const GZSndWaveFormat* GetWaveFormat() const = 0;                 // [18] +0x90
    virtual bool DataIsValid() = 0;                                           // [19] +0x98
    virtual bool SetDataIsValid(bool valid) = 0;                              // [20] +0xa0
    virtual std::uint32_t GetPosition() const = 0;                            // [21] +0xa8
    virtual bool SetPosition(std::uint32_t position) = 0;                     // [22] +0xb0
};

constexpr std::uint32_t kGZIID_cIGZSndDataSource = 0x3afd825f;
