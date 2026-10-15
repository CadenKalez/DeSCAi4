#include "game/GameData.h"

#include <algorithm>
#include <dirent.h>

#include "gz/cGZDBSegmentPackedFile.h"
#include "rz/cRZString.h"
#include "sc4/DBSegment.h"

namespace game {

GameData::~GameData() {
    sc4::SetPersistResourceManagerService(nullptr);
    if (manager_) manager_->Shutdown();
    for (auto* s : segments_) {
        s->Close();
        s->Release();
    }
    if (manager_) manager_->Release();
}

bool GameData::Open(const std::string& dir, std::string& error) {
    std::vector<std::string> names;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 4 && n.substr(n.size() - 4) == ".dat" && n != "Sound.dat" && n != "Intro.dat" && n != "Music.dat")
                names.push_back(n);
        }
        closedir(d);
    }
    if (names.empty()) { error = "no .dat packages in " + dir; return false; }
    std::sort(names.begin(), names.end());
    manager_ = new cGZPersistResourceManager();
    manager_->AddRef();
    for (const std::string& n : names) {
        auto* segment = new cGZDBSegmentPackedFile();
        segment->AddRef();
        segment->Init();
        cRZString path((dir + "/" + n).c_str());
        segment->SetPath(path);
        if (!segment->Open(true, false)) { segment->Release(); continue; }
        manager_->RegisterDBSegmentBack(*segment);     // earlier packages win
        segments_.push_back(segment);
    }
    exemplarFactory_ = new sc4::cSCResExemplarFactory();
    exemplarFactory_->AddRef();
    cohortFactory_ = new sc4::cSCResExemplarCohortFactory();
    cohortFactory_->AddRef();
    manager_->RegisterObjectFactory(0x0e5ee001, sc4::kResourceTypeExemplar, exemplarFactory_);
    manager_->RegisterObjectFactory(0x0e5ee002, sc4::kResourceTypeCohort, cohortFactory_);
    sc4::SetPersistResourceManagerService(manager_);
    if (segments_.empty()) { error = "could not open any package"; return false; }
    return true;
}

sc4::cISCResExemplar* GameData::GetExemplar(const cGZPersistResourceKey& key) {
    void* raw = nullptr;
    if (!manager_ || !manager_->GetResource(key, sc4::kGZIID_cISCResExemplar, &raw, 0, nullptr)) return nullptr;
    return static_cast<sc4::cISCResExemplar*>(raw);
}

bool GameData::ReadRecord(const cGZPersistResourceKey& key, std::vector<std::uint8_t>& bytes) {
    for (auto* segment : segments_) {
        cIGZPersistDBRecord* raw = nullptr;
        if (!segment->OpenRecord(key, &raw, 1)) continue;
        void* serial = nullptr;
        raw->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &serial);
        auto* record = static_cast<cIGZPersistDBSerialRecord*>(serial);
        bytes.resize(record->GetSize());
        const bool ok = record->GetFieldVoid(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
        record->Release();
        segment->CloseRecord(raw);
        raw->Release();
        if (ok) return true;
    }
    return false;
}

}  // namespace game
