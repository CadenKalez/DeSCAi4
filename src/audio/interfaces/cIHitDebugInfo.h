// cIHitDebugInfo — source-level debug information for loaded Hit track files (map between track
// data addresses and (source file, line) pairs). Slots 3..13 (+0x18..+0x68) from cHitDebugInfo
// (vtable 0x100ab5c50); interface id 0x7bba0cbd, service id 0xfbc9cd04.
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"

class cIHitDebugInfo : public cIGZUnknown {
public:
    virtual bool Shutdown() = 0;                                                              // [3]
    virtual void AddSourceFile(cIGZString& fileName, const std::uint8_t* data, std::int32_t size) = 0;   // [4]
    virtual bool GetDataAddressFromFilenameAndLineNum(cIGZString& fileName, std::int32_t line, const std::uint8_t** out) = 0;   // [5]
    virtual bool GetFilenameAndLineNumFromDataAddress(const std::uint8_t* address, cIGZString& fileName, std::int32_t& line) = 0;   // [6]
    virtual bool GetDataAddressFromEntryPoint(std::uint32_t entryPoint, const std::uint8_t** out) = 0;   // [7]
    virtual bool GetHitFilenameFromEntryPoint(std::uint32_t entryPoint, cIGZString& fileName) = 0;       // [8]
    virtual bool LoadDebugInfo(cIGZString& hitFileName, const std::uint8_t* data, std::int32_t size) = 0;  // [9]
    virtual bool LoadSymbolFile(cIGZString& fileName) = 0;                                    // [10]
    virtual bool Init() = 0;                                                                  // [11]
    virtual std::int32_t GetServicePriority() const = 0;                                      // [12]
    virtual void AddSourceLine(std::int32_t line, std::int32_t offset) = 0;                   // [13]
};
constexpr std::uint32_t kGZIID_cIHitDebugInfo = 0x7bba0cbd;
constexpr std::uint32_t kGZSID_cHitDebugInfo = 0xfbc9cd04;
