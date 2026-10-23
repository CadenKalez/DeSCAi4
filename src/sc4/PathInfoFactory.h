// cSC4PathInfoFactory — the persist-resource factory for cSC4PathInfo (type 0x296678f7, 8 functions, 0x10045cdd8..0x10045d96a).
// It builds empty path infos (CreateInstance) and fills them from the game's TEXT path files (SerialRead; SerialWrite is a stub
// that returns false). Text format, one value per line, lines starting with "--" are comments and skipped:
//   SC4PATHS / version (float; 1.1 adds the stop point count, 1.2 adds a per-path flags value) / path count (<= 1000) /
//   [stop point count (<= 1000), version >= 1.1] / use-elevation flag, then per path: type (1..7), lane, entry edge (0..3 or 255),
//   exit edge (0..3 or 255), [flags, version >= 1.2], point count (>= 1), then one "x,z,y" line per point (z is stored negated);
//   then per stop point: kind (1 or 2), type (1..7), lane, entry, exit, "x,z,y". Finally MakeBidirectionalPaths runs.
// Quirks kept: a stop point's key gets bit 29 (0x20000000) instead of bit 28 when (kind != 1) differs from right-hand traffic;
// a point closer than 1e-5 to the previous one is dropped (but still counted); a failed read leaves the path info half filled;
// a "\r" ends a line and swallows the next byte too.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "gz/StreamOps.h"
#include "gz/cGZPersistResourceManager.h"
#include "sc4/PathInfo.h"

namespace sc4 {

constexpr std::uint32_t kPathInfoResourceType = 0x296678f7;

// Byte source for the text reader (the serial record: GetFieldUint8 slot 0x28 and the size at slot 0x1a0).
class IPathTextSource {
public:
    virtual ~IPathTextSource() = default;
    virtual bool ReadByte(std::uint8_t& byte) = 0;
};

class cSC4PathInfoFactory : public cGZPersistResourceFactory {
public:
    explicit cSC4PathInfoFactory(std::function<bool()> rightHandTraffic = nullptr) : rightHand_(std::move(rightHandTraffic)) {}
    using cGZPersistResourceFactory::CreateInstance;
    bool CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) override;   // 0x10045ce24
    bool SerialRead(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) override;    // 0x10045d23e
    bool SerialWrite(cIGZPersistResource&, cIGZPersistDBSerialRecord&) override { return false; }  // 0x10045d96a

    // The text parser behind SerialRead; `remaining` is the byte budget (the record size).
    bool ParsePathText(IPathTextSource& source, std::uint32_t remaining, cSC4PathInfo& info, bool rightHand);   // body of 0x10045d23e
    // 0x10045cf06 / 0x10045d00e / 0x10045d096
    bool ReadString(IPathTextSource& source, std::string& line, std::uint32_t& remaining);
    bool ReadUint32(IPathTextSource& source, std::uint32_t& value, std::uint32_t& remaining);
    bool ReadVector3(IPathTextSource& source, cS3DVector3& value, std::uint32_t& remaining);

private:
    std::function<bool()> rightHand_;
};

}  // namespace sc4
