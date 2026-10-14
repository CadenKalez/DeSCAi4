#include "audio/FreshDimensionPlayer.h"

#include <algorithm>

cFreshDimensionPlayer::cFreshDimensionPlayer() {
    divisionEnabled_[0][1] = divisionEnabled_[1][0] = divisionEnabled_[1][2] = divisionEnabled_[2][1] = true;
    for (std::uint32_t x = 0; x <= numDivisionsX_; ++x) {
        for (std::uint32_t z = 0; z <= numDivisionsZ_; ++z) {
            divisionPosX_[x][z] = static_cast<std::int32_t>(minDistance_) * (static_cast<std::int32_t>(x) - 1);
            divisionPosY_[x][z] = 0;
            divisionPosZ_[x][z] = static_cast<std::int32_t>(minDistance_) * (1 - static_cast<std::int32_t>(z));
        }
    }
}

cFreshDimensionPlayer::~cFreshDimensionPlayer() = default;

// cFreshDimensionPlayer::QueryInterface @ 0x1004cfc62
bool cFreshDimensionPlayer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIFreshPlayer) {
        *out = static_cast<cIFreshPlayer*>(this);
    } else if (iid == kGZIID_cIFreshDimensionPlayer || iid == kGZIID_cIGZUnknown) {
        *out = static_cast<cIFreshDimensionPlayer*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

// cFreshDimensionPlayer::Release @ 0x1004cfc06 — the count is never driven below 1 by decrement:
// the call that sees it at 0 or 1 destroys the object.
std::uint32_t cFreshDimensionPlayer::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

template <class F>
void cFreshDimensionPlayer::ForEachPlayer(F&& f) {
    for (auto& entry : players_) f(entry.first, *entry.second.get());
}

// cFreshDimensionPlayer::Init() @ 0x1004cfcb2 — takes (a reference to) the sound system.
bool cFreshDimensionPlayer::InitializeFromLoadedScore() {
    Fresh::cIFreshHost* host = Fresh::GetHost();
    hasSoundSystem_ = host != nullptr && host->HasSoundSystem();
    return hasSoundSystem_;
}

// cFreshDimensionPlayer::LoadScore @ 0x1004cfec8 — creates a cFreshPlayer through COM, loads the
// score into it and registers it under `scoreId` (disabled until SetScoreEnabled).
bool cFreshDimensionPlayer::LoadScore(const cIGZString& fileName, std::uint32_t scoreId) {
    Fresh::cIFreshHost* host = Fresh::GetHost();
    cIFreshPlayer* created = host != nullptr ? host->CreateFreshPlayerInstance() : nullptr;
    if (created == nullptr) return false;
    cRZAutoRefCount<cIFreshPlayer> local;
    local = created;       // takes its own reference ...
    created->Release();    // ... and drops the creation reference
    if (!local->LoadScore(fileName, 0)) return false;

    players_[scoreId] = local.get();
    scores_[scoreId] = local->GetScore(0);
    SetScoreEnabled(scoreId, false);
    local->SetVolume(volume_);
    local->SetXPos(xPos_);
    local->SetYPos(yPos_);
    local->SetZoom(zoom_);
    return true;
}

// cFreshDimensionPlayer::GetScore @ 0x1004d01fc
cIFreshScore* cFreshDimensionPlayer::GetScore(std::uint32_t scoreId) {
    const auto it = scores_.find(scoreId);
    return it == scores_.end() ? nullptr : it->second.get();
}

// cFreshDimensionPlayer::Play @ 0x1004cfd14
bool cFreshDimensionPlayer::Play() {
    if (!playing_) {
        if (Fresh::cIFreshHost* host = Fresh::GetHost()) {
            host->AddUpdateTarget(static_cast<cIGZSndUpdateTarget*>(this), 250);
        }
        timer_.Reset();
        timer_.Restart();
        playStartTime_ = lastStartTime_ = timer_.GetElapsedTimeU32();
        playing_ = true;
    }
    return true;
}

// cFreshDimensionPlayer::Stop @ 0x1004cfd88 — leaves running cell players to finish by themselves.
bool cFreshDimensionPlayer::Stop() {
    if (playing_) {
        if (Fresh::cIFreshHost* host = Fresh::GetHost()) {
            host->RemoveUpdateTarget(static_cast<cIGZSndUpdateTarget*>(this));
        }
        playing_ = false;
    }
    return true;
}

// cFreshDimensionPlayer::SetVolume @ 0x1004cfe20 — applied to the cell players already started.
bool cFreshDimensionPlayer::SetVolume(std::int32_t volume) {
    volume_ = volume;
    for (cIFreshCellPlayer* cp : active_) {
        if (cp != nullptr) cp->SetVol(volume_);
    }
    return true;
}

// cFreshDimensionPlayer::FadeVolume @ 0x1004cfdf4 — only records the request; the original never
// acts on the two flags (the duration and target are ignored).
bool cFreshDimensionPlayer::FadeVolume(std::int32_t, std::int32_t, bool stopAtEnd) {
    fadeRequested_ = true;
    stopAtFadeEnd_ = stopAtEnd;
    return true;
}

bool cFreshDimensionPlayer::SetPlayPercent(std::int32_t percent) {  // @ 0x1004d0f38
    playPercent_ = percent;
    UpdateCellPlayerMap();
    return true;
}

// cFreshDimensionPlayer::SetXPos/SetYPos/SetZoom @ 0x1004d0472/0x1004d0504/0x1004d0596 — forwarded
// to every loaded player, then the pool is rebuilt (the selection areas moved).
bool cFreshDimensionPlayer::SetXPos(std::int32_t position) {
    xPos_ = position;
    ForEachPlayer([position](std::uint32_t, cIFreshPlayer& p) { p.SetXPos(position); });
    UpdateCellPlayerMap();
    return true;
}
bool cFreshDimensionPlayer::SetYPos(std::int32_t position) {
    yPos_ = position;
    ForEachPlayer([position](std::uint32_t, cIFreshPlayer& p) { p.SetYPos(position); });
    UpdateCellPlayerMap();
    return true;
}
bool cFreshDimensionPlayer::SetZoom(std::int32_t zoom) {
    zoom_ = zoom;
    ForEachPlayer([zoom](std::uint32_t, cIFreshPlayer& p) { p.SetZoom(zoom); });
    UpdateCellPlayerMap();
    return true;
}

// cFreshDimensionPlayer::Reseed @ 0x1004d03c8 — the own generator is re-seeded once per player.
bool cFreshDimensionPlayer::Reseed(std::uint32_t seed) {
    ForEachPlayer([this, seed](std::uint32_t, cIFreshPlayer& p) {
        p.Reseed(seed);
        random_.Seed(seed);
    });
    UpdateCellPlayerMap();
    return true;
}

bool cFreshDimensionPlayer::SetNumDivisionsX(std::uint32_t count) {  // @ 0x1004d0e6c
    if (count < 3) numDivisionsX_ = count;
    return count < 3;
}
bool cFreshDimensionPlayer::SetNumDivisionsZ(std::uint32_t count) {  // @ 0x1004d0e9c
    if (count < 3) numDivisionsZ_ = count;
    return count < 3;
}
bool cFreshDimensionPlayer::SetDivisionEnabled(std::uint32_t x, std::uint32_t z, bool enabled) {  // @ 0x1004d0dac
    if (x >= 3 || z >= 3) return false;
    divisionEnabled_[x][z] = enabled;
    return true;
}
bool cFreshDimensionPlayer::SetDivision3DPosition(std::uint32_t x, std::uint32_t z, std::int32_t px, std::int32_t py,
                                                  std::int32_t pz) {  // @ 0x1004d0df8
    if (x >= 3 || z >= 3) return false;
    divisionPosX_[x][z] = px;
    divisionPosY_[x][z] = py;
    divisionPosZ_[x][z] = pz;
    return true;
}

// cFreshDimensionPlayer::SetScoreEnabled @ 0x1004d0266 — disabling a score stops every cell player
// in its selection area before the pool is rebuilt.
bool cFreshDimensionPlayer::SetScoreEnabled(std::uint32_t scoreId, bool enabled) {
    enabled_[scoreId] = enabled;
    if (!enabled) {
        const auto it = players_.find(scoreId);
        if (it != players_.end() && it->second.get() != nullptr) {
            cRZAutoRefCount<cIFreshPlayer> player = it->second;  // keep alive while stopping
            player->GetScore(0);
            const std::int32_t firstRow = player->GetFirstSelectableRow();
            const std::int32_t lastRow = player->GetLastSelectableRow();
            const std::int32_t firstCol = player->GetFirstSelectableColumn();
            const std::int32_t lastCol = player->GetLastSelectableColumn();
            for (std::int32_t row = firstRow; row <= lastRow; ++row) {
                for (std::int32_t col = firstCol; col <= lastCol; ++col) {
                    if (cIFreshCellPlayer* cp = player->GetCellPlayer(static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(col), 0)) {
                        cp->Stop();
                    }
                }
            }
        }
    }
    UpdateCellPlayerMap();
    return true;
}

// cFreshDimensionPlayer::UpdateCellPlayerMap @ 0x1004d0628 — rebuilds the pool of every cell
// player in the selection areas of the enabled scores (score id 0 is never used) and their summed
// probability weight.
void cFreshDimensionPlayer::UpdateCellPlayerMap() {
    Fresh::cIFreshHost* host = Fresh::GetHost();
    if (hasSoundSystem_ && host != nullptr && host->IsSoundSystemInTestMode()) return;

    pool_.clear();
    poolCells_.clear();
    poolCount_ = 0;
    totalWeight_ = 0;
    enabledScores_ = 0;
    for (auto& [scoreId, playerRef] : players_) {
        if (scoreId == 0) continue;
        const auto en = enabled_.find(scoreId);
        if (en == enabled_.end() || !en->second) continue;
        ++enabledScores_;
        cIFreshPlayer* player = playerRef.get();
        if (player == nullptr) continue;
        player->GetScore(0);
        const std::int32_t firstRow = player->GetFirstSelectableRow();
        const std::int32_t lastRow = player->GetLastSelectableRow();
        const std::int32_t firstCol = player->GetFirstSelectableColumn();
        const std::int32_t lastCol = player->GetLastSelectableColumn();
        for (std::int32_t row = firstRow; row <= lastRow; ++row) {
            for (std::int32_t col = firstCol; col <= lastCol; ++col) {
                cIFreshCellPlayer* cp = player->GetCellPlayer(static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(col), 0);
                if (cp == nullptr) continue;
                pool_[static_cast<std::uint32_t>(poolCount_)] = cp;
                if (cIFreshCell* cell = cp->GetCell()) {
                    poolCells_[static_cast<std::uint32_t>(poolCount_)] = cell;
                    totalWeight_ += cell->GetProbability();
                }
                ++poolCount_;
            }
        }
    }
}

// cFreshDimensionPlayer::CalculateCellplayer3DPosition @ 0x1004d0c26 — same division draw as
// cFreshPlayer's (each enabled division weighs 1000, the previous one is skipped).
bool cFreshDimensionPlayer::CalculateCellplayer3DPosition(cIFreshCellPlayer* cellPlayer) {
    std::int32_t enabledCount = 0;
    for (std::uint32_t x = 0; x <= numDivisionsX_; ++x)
        for (std::uint32_t z = 0; z <= numDivisionsZ_; ++z) enabledCount += divisionEnabled_[x][z] ? 1 : 0;
    if (enabledCount == 0) return false;

    std::uint32_t chosenX = lastDivisionX_, chosenZ = lastDivisionZ_;
    std::uint32_t draw = random_.RandomUint32Uniform(static_cast<std::uint32_t>(enabledCount) * 1000u);
    bool found = false;
    for (std::uint32_t x = 0; x <= numDivisionsX_ && !found; ++x) {
        for (std::uint32_t z = 0; z <= numDivisionsZ_ && !found; ++z) {
            if (divisionEnabled_[x][z] && (x != lastDivisionX_ || z != lastDivisionZ_)) {
                if (draw < 1000) {
                    chosenX = x;
                    chosenZ = z;
                    found = true;
                } else {
                    draw -= 1000;
                }
            }
        }
    }
    lastDivisionX_ = chosenX;
    lastDivisionZ_ = chosenZ;
    cellPlayer->SetHeadRelative(headRelative_);
    cellPlayer->SetMinMaxDistance(minDistance_, maxDistance_);
    cellPlayer->Set3DPosition(divisionPosX_[chosenX][chosenZ], divisionPosY_[chosenX][chosenZ], divisionPosZ_[chosenX][chosenZ]);
    return true;
}

// cFreshDimensionPlayer::Update @ 0x1004d0a04 — per sound-system tick: retire one finished cell
// player; after max(2, 7 - enabledScores) seconds start one more (at most four at a time) chosen
// by a weighted draw over the pool; then update every running cell player.
void cFreshDimensionPlayer::Update(std::int32_t) {
    Fresh::cIFreshHost* host = Fresh::GetHost();
    if (hasSoundSystem_ && host != nullptr && host->IsSoundSystemInTestMode()) return;

    const auto finished = std::find_if(active_.begin(), active_.end(), [](cIFreshCellPlayer* cp) { return !cp->IsPlaying(); });
    if (finished != active_.end()) active_.erase(finished);

    if (volume_ != 0) {
        const std::int32_t seconds = std::max(2, 7 - enabledScores_);
        const std::uint32_t elapsed = timer_.GetElapsedTimeU32();
        if (static_cast<std::uint32_t>(seconds) * 1000u < elapsed - lastStartTime_) {
            if (active_.size() < 4) {
                const std::uint32_t pick = random_.RandomUint32Uniform(static_cast<std::uint32_t>(totalWeight_));
                std::uint32_t accumulated = 0;
                for (auto& [index, cp] : pool_) {
                    if (cp == nullptr) continue;
                    cIFreshCell* cell = cp->GetCell();
                    if (cell == nullptr) continue;
                    accumulated += static_cast<std::uint32_t>(cell->GetProbability());
                    if (pick <= accumulated) {
                        active_.push_back(cp);
                        cp->SetVol(volume_);
                        CalculateCellplayer3DPosition(cp);
                        cp->Start();
                        break;
                    }
                }
            }
            timer_.Reset();
            timer_.Restart();
            lastStartTime_ = 0;
        }
    }
    for (cIFreshCellPlayer* cp : active_) {
        if (cp != nullptr) cp->Update();
    }
}
