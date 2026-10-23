#include "sc4/PathInfoFactory.h"

#include <cmath>

#include "gz/cIGZPersistDBSerialRecord.h"
#include "rz/RZConvert.h"
#include "rz/RZStringUtils.h"

namespace sc4 {

namespace {
class RecordSource final : public IPathTextSource {
public:
    explicit RecordSource(cIGZPersistDBSerialRecord& record) : record_(record) {}
    bool ReadByte(std::uint8_t& byte) override { return record_.GetFieldUint8(byte); }
private:
    cIGZPersistDBSerialRecord& record_;
};

// cRZString::SplitToken(",") on a std::string: the text before the first comma is removed (with the comma) and returned; with no
// comma the token is empty and the line stays.
std::string SplitComma(std::string& line) {
    const std::size_t pos = line.find(',');
    if (pos == std::string::npos) return std::string();
    std::string token = line.substr(0, pos);
    line.erase(0, pos + 1);
    return token;
}
}  // namespace

// 0x10045ce24: only the path info type; the iid picks the interface (path info 0xa9667793 or persist resource 0x456b8f1d).
bool cSC4PathInfoFactory::CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) {
    if (out) *out = nullptr;
    if (type != kPathInfoResourceType) return false;
    cSC4PathInfo* info = new cSC4PathInfo();
    void* found = nullptr;
    if (iid == kGZIID_cISC4PathInfo) found = static_cast<cISC4PathInfo*>(info);
    else if (iid == kGZIID_PathInfoPersistResource) found = static_cast<cGZPersistResource*>(info);
    if (!found) {
        delete info;
        return false;
    }
    if (out) *out = found;
    info->AddRef();
    return true;
}

// 0x10045cf06: reads one line (LF ends it; CR ends it and swallows one more byte), skipping lines that start with "--".
// False when the (final) line is empty.
bool cSC4PathInfoFactory::ReadString(IPathTextSource& source, std::string& line, std::uint32_t& remaining) {
    for (;;) {
        line.clear();
        while (remaining != 0) {
            std::uint8_t byte = 0;
            source.ReadByte(byte);
            --remaining;
            if (byte == '\n') break;
            if (byte == '\r') {
                source.ReadByte(byte);
                --remaining;
                break;
            }
            line.push_back(char(byte));
            if (remaining == 0) break;
        }
        if (!line.empty() && line.compare(0, 2, "--") == 0) continue;
        return !line.empty();
    }
}

// 0x10045d00e
bool cSC4PathInfoFactory::ReadUint32(IPathTextSource& source, std::uint32_t& value, std::uint32_t& remaining) {
    std::string line;
    value = 0;
    const bool ok = ReadString(source, line, remaining);
    if (ok) value = RZ::ConvertToUint(line.c_str());
    return ok;
}

// 0x10045d096: "x,z,y" - the file's second number is the NEGATED z of the vector, the rest of the line is y.
bool cSC4PathInfoFactory::ReadVector3(IPathTextSource& source, cS3DVector3& value, std::uint32_t& remaining) {
    std::string line;
    value = cS3DVector3{};
    const bool ok = ReadString(source, line, remaining);
    if (!ok) return false;
    std::string token = SplitComma(line);
    value.x = float(RZAtofEnglishA(token.c_str()));
    token = SplitComma(line);
    value.z = -float(RZAtofEnglishA(token.c_str()));
    value.y = float(RZAtofEnglishA(line.c_str()));
    return true;
}

bool cSC4PathInfoFactory::ParsePathText(IPathTextSource& source, std::uint32_t remaining, cSC4PathInfo& info, bool rightHand) {
    std::string line;
    if (!ReadString(source, line, remaining) || line != "SC4PATHS") return false;
    if (!ReadString(source, line, remaining)) return false;
    const float version = float(RZAtofEnglishA(line.c_str()));
    std::uint32_t pathCount = 0, stopCount = 0, elevation = 0;
    bool ok = ReadUint32(source, pathCount, remaining);
    if (pathCount > 1000 || !ok) return false;
    if (1.1 <= version) {
        ok = ReadUint32(source, stopCount, remaining);
        if (stopCount > 1000 || !ok) return false;
    }
    if (!ReadUint32(source, elevation, remaining)) return false;
    info.SetUseElevationFlag(elevation != 0);

    for (std::uint32_t p = 0; p < pathCount; ++p) {
        std::uint32_t type = 0, lane = 0, entry = 0, exit = 0, flags = 0, pointCount = 0;
        ok = ReadUint32(source, type, remaining);
        if (type - 1 > 6 || !ok) return false;
        if (!ReadUint32(source, lane, remaining)) return false;
        if (!ReadUint32(source, entry, remaining) || (entry > 3 && entry != 0xff)) return false;
        if (!ReadUint32(source, exit, remaining) || (exit > 3 && exit != 0xff)) return false;
        if (1.2 <= version && !ReadUint32(source, flags, remaining)) return false;
        ok = ReadUint32(source, pointCount, remaining);
        if (pointCount == 0 || !ok) return false;
        tPath* path = info.NewPath((exit & 0xff) | ((entry & 0xff) << 8) | (type << 24) | ((lane & 0xff) << 16));
        if (!path) return false;
        path->points.reserve(pointCount);
        cS3DVector3 previous;
        for (std::uint32_t i = 0; i < pointCount; ++i) {
            cS3DVector3 point;
            if (!ReadVector3(source, point, remaining)) return false;
            if (i == 0) {
                path->points.push_back(point);
            } else {
                const float dx = point.x - previous.x, dy = point.y - previous.y, dz = point.z - previous.z;
                const float distance = std::sqrt(dz * dz + dx * dx + dy * dy);
                if (1e-05f < distance) {
                    path->points.push_back(point);
                    path->length += distance;
                }
            }
            path->flags = std::uint8_t(flags);
            previous = point;
        }
    }
    for (std::uint32_t s = 0; s < stopCount; ++s) {
        std::uint32_t kind = 1, type = 0, lane = 0, entry = 0, exit = 0;
        if (!ReadUint32(source, kind, remaining)) return false;
        if (kind - 1 > 1) return false;
        ok = ReadUint32(source, type, remaining);
        if (type - 1 > 6 || !ok) return false;
        if (!ReadUint32(source, lane, remaining)) return false;
        if (!ReadUint32(source, entry, remaining) || (entry > 3 && entry != 0xff)) return false;
        if (!ReadUint32(source, exit, remaining) || (exit > 3 && exit != 0xff)) return false;
        cS3DVector3 point;
        if (!ReadVector3(source, point, remaining)) return false;
        const std::uint32_t stopFlag = ((kind != 1) != rightHand) ? 0x20000000u : 0x10000000u;
        info.NewStopPoint(point, (exit & 0xff) | ((entry & 0xff) << 8) | (type << 24) | stopFlag | ((lane & 0xff) << 16));
    }
    info.MakeBidirectionalPaths();
    return true;
}

// 0x10045d23e
bool cSC4PathInfoFactory::SerialRead(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) {
    if (resource.GetType() != kPathInfoResourceType) return false;
    cSC4PathInfo* info = dynamic_cast<cSC4PathInfo*>(&resource);
    if (!info) return false;
    RecordSource source(record);
    return ParsePathText(source, record.GetSize(), *info, rightHand_ ? rightHand_() : true);
}

}  // namespace sc4
