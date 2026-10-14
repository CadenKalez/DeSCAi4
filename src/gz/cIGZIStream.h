// cIGZIStream — sequential binary input. Slot layout (3..20) recovered from the vtables of
// cRZIStream and cRZNullIStream. Get* return true on success; after a failure GetError() is true.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "gz/cIGZVariant.h"

class cIGZSerializable;

class cIGZIStream : public cIGZUnknown {
public:
    virtual bool Skip(std::uint32_t byteCount) = 0;                          // [3]
    virtual bool GetSint8(std::int8_t& value) = 0;                           // [4]
    virtual bool GetUint8(std::uint8_t& value) = 0;                          // [5]
    virtual bool GetSint16(std::int16_t& value) = 0;                         // [6]
    virtual bool GetUint16(std::uint16_t& value) = 0;                        // [7]
    virtual bool GetSint32(std::int32_t& value) = 0;                         // [8]
    virtual bool GetUint32(std::uint32_t& value) = 0;                        // [9]
    virtual bool GetSint64(std::int64_t& value) = 0;                         // [10]
    virtual bool GetUint64(std::uint64_t& value) = 0;                        // [11]
    virtual bool GetFloat32(float& value) = 0;                               // [12]
    virtual bool GetFloat64(double& value) = 0;                              // [13]
    // [14] Reads a length-prefixed string into `buffer` (capacity maxLength + 1); excess is skipped.
    virtual bool GetRZCharStr(char* buffer, std::uint32_t maxLength) = 0;
    virtual bool GetGZStr(cIGZString& value) = 0;                            // [15]
    virtual bool GetGZSerializable(cIGZSerializable& object) = 0;            // [16] object.Read(*this)
    virtual bool GetVoid(void* destination, std::uint32_t byteCount) = 0;    // [17] raw bytes
    virtual bool GetError() const = 0;                                       // [18]
    virtual void SetUserData(cIGZVariant* userData) = 0;                     // [19] ref-counted
    virtual cIGZVariant* GetUserData() = 0;                                  // [20]
    // [21],[22] destructors; [23] Init(bool), [24] Shutdown() belong to cRZIStream.
};

constexpr std::uint32_t kGZIID_cIGZIStream = 0x051b11bd;
