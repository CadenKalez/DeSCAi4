#include "sc4/CityBuildings.h"

#include <cstring>

#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZPersistResourceKeyList.h"
#include "rz/PortMemoryStreams.h"
#include "rz/cRZString.h"
#include "sc4/BuildingOccupant.h"

namespace sc4 {

namespace {
bool ParseEntry(const std::uint8_t* body, std::size_t length, BuildingRecord& rec) {
    PortMemoryIStream stream(body, length);
    cSC4BuildingOccupant b;
    b.AddRef();
    if (!b.Read(stream) || stream.Position() != length) return false;
    rec.buildingType = b.GetBuildingType();
    rec.exemplar = b.DefaultExemplarKey();
    b.GetBoundingBox(rec.min, rec.max);
    rec.orientation = b.GetOrientation();
    rec.lit = b.IsLit();
    rec.completion = b.GetCompletionPercent();
    return true;
}
}  // namespace

bool LoadCityBuildings(const std::string& cityPath, std::vector<BuildingRecord>& out, std::string& error) {
    auto* segment = new cGZDBSegmentPackedFile();
    segment->AddRef();
    segment->Init();
    cRZString path(cityPath.c_str());
    segment->SetPath(path);
    if (!segment->Open(true, false)) {
        error = "cannot open " + cityPath;
        segment->Release();
        return false;
    }
    auto* keys = new cGZPersistResourceKeyList();
    keys->AddRef();
    cGZPersistResourceKeyFilterByType filter(kGZCLSID_cSC4BuildingOccupant);
    filter.AddRef();
    segment->GetResourceKeyList(keys, &filter);
    for (std::uint32_t i = 0; i < keys->Size(); ++i) {
        cIGZPersistDBRecord* raw = nullptr;
        if (!segment->OpenRecord(*keys->GetKey(i), &raw, 1)) continue;
        void* serial = nullptr;
        raw->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &serial);
        auto* record = static_cast<cIGZPersistDBSerialRecord*>(serial);
        std::vector<std::uint8_t> bytes(record->GetSize());
        record->GetFieldVoid(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
        std::size_t pos = 0;
        while (pos + 12 <= bytes.size()) {
            std::uint32_t size;
            std::memcpy(&size, &bytes[pos], 4);
            if (size < 16 || pos + size > bytes.size()) break;
            BuildingRecord rec;
            bool ok = false;
            for (int idWidth : {8, 4}) {      // segment version >= 0x42 stores 64-bit object ids
                if (size < 8 + std::size_t(idWidth)) continue;
                const std::uint8_t* idp = &bytes[pos + 8];
                std::uint64_t id = 0;
                std::memcpy(&id, idp, idWidth);
                if (ParseEntry(idp + idWidth, size - 8 - idWidth, rec)) {
                    rec.objectId = id;
                    ok = true;
                    break;
                }
            }
            if (ok) out.push_back(rec);
            pos += size;
        }
        record->Release();
        segment->CloseRecord(raw);
        raw->Release();
    }
    segment->Close();
    segment->Release();
    return true;
}

}  // namespace sc4
