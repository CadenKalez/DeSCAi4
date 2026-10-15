// PORT-ONLY: the game's data packages (SimCity_1..5.dat, EP1.dat ...) mounted in one recovered
// cGZPersistResourceManager with the exemplar/cohort factories registered, so resources resolve the way
// they do in the original (parent cohorts, default exemplars).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZPersistResourceManager.h"
#include "sc4/Exemplar.h"

namespace game {

class GameData {
public:
    ~GameData();
    // Opens every *.dat under `dir` (later packages shadow earlier ones in the order given by `order`).
    bool Open(const std::string& dir, std::string& error);
    cGZPersistResourceManager* Manager() { return manager_; }

    // resolve an exemplar (AddRef'd); null when missing
    sc4::cISCResExemplar* GetExemplar(const cGZPersistResourceKey& key);
    // raw bytes of any resource (the record contents)
    bool ReadRecord(const cGZPersistResourceKey& key, std::vector<std::uint8_t>& bytes);

private:
    cGZPersistResourceManager* manager_ = nullptr;
    std::vector<cGZDBSegmentPackedFile*> segments_;
    sc4::cSCResExemplarFactory* exemplarFactory_ = nullptr;
    sc4::cSCResExemplarCohortFactory* cohortFactory_ = nullptr;
};

}  // namespace game
