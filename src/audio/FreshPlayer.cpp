#include "audio/FreshPlayer.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "audio/FreshTables.h"

namespace {

struct SectionGuard {
    cRZCriticalSection& section;
    explicit SectionGuard(cRZCriticalSection& s) : section(s) { section.Lock(); }
    ~SectionGuard() { section.Unlock(); }
};

constexpr std::int32_t kMaxVolume = 0x400;

}  // namespace

// g_FreshSection @ 0x100b8c9a8: one lock shared by every cFreshPlayer.
cRZCriticalSection& cFreshPlayer::Section() {
    static cRZCriticalSection section;
    return section;
}

// cFreshPlayer::cFreshPlayer() @ 0x1004ccd8e
cFreshPlayer::cFreshPlayer() : random_(std::make_unique<cRZRandom>(0xFFFFFFFFu)) {
    divisionEnabled_ = {};
    divisionEnabled_[0][1] = divisionEnabled_[1][0] = divisionEnabled_[1][2] = divisionEnabled_[2][1] = true;
    for (std::uint32_t x = 0; x <= numDivisionsX_; ++x) {
        for (std::uint32_t z = 0; z <= numDivisionsZ_; ++z) {
            divisionPosX_[x][z] = static_cast<std::int32_t>(minDistance_) * (static_cast<std::int32_t>(x) - 1);
            divisionPosY_[x][z] = 0;
            divisionPosZ_[x][z] = static_cast<std::int32_t>(minDistance_) * (1 - static_cast<std::int32_t>(z));
        }
    }
}

cFreshPlayer::~cFreshPlayer() {
    Shutdown();
    if (external_ != nullptr) {
        external_->Release();
        external_ = nullptr;
    }
}

// cFreshPlayer::QueryInterface(unsigned int, void**) @ 0x1004cd156 — note: cIGZUnknown's id 1 is
// NOT accepted by this class.
bool cFreshPlayer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != kGZIID_cIFreshPlayer) return false;
    *out = static_cast<cIFreshPlayer*>(this);
    AddRef();
    return true;
}

// cFreshPlayer::Release() @ 0x1004cd1b2
std::uint32_t cFreshPlayer::Release() {
    if (refCount_ == 1) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

template <class F>
void cFreshPlayer::ForEachCellPlayer(F&& f) {
    if (score_ == nullptr) return;
    for (int row = 0; row < score_->header.rows && row < cFreshScore::kMaxRows; ++row) {
        for (int col = 0; col < score_->header.columns && col < cFreshScore::kMaxColumns; ++col) {
            if (cFreshCellPlayer* cp = grid_[row][col]) f(row, col, *cp);
        }
    }
}

// ---- initialisation / shutdown --------------------------------------------------------------

// cFreshPlayer::Init(cRZString const&) @ 0x1004cd1f6 — splits the path into directory and file.
bool cFreshPlayer::InitializeFromPath(const cRZString& path) {
    const std::string& p = path.str();
    const auto sep = p.find_last_of("/\\");
    const std::string dir = sep == std::string::npos ? std::string() : p.substr(0, sep + 1);
    const std::string file = sep == std::string::npos ? p : p.substr(sep + 1);
    return InitializeFromDirectoryAndFile(cRZString(dir.c_str()), cRZString(file.c_str()));
}

// cFreshPlayer::Init(cRZString const&, cRZString const&) @ 0x1004cd39c
bool cFreshPlayer::InitializeFromDirectoryAndFile(const cRZString& directory, const cRZString& fileName) {
    auto* score = cFreshScore::Create();
    score->LoadScoreFromPath(directory.str() + fileName.str());  // result ignored, as in the original
    return InitializeWithScoreAndDataDir(score, directory);
}

// cFreshPlayer::Init(cFreshScore*, cRZString const&) @ 0x1004cd720
bool cFreshPlayer::InitializeWithScoreAndDataDir(cFreshScore* score, const cRZString& dataDir) {
    SectionGuard guard(Section());
    if (score_ != score) {
        // Port deviation: the player keeps its own reference on the score. The original stores the raw
        // pointer and deletes it directly, which double-frees when another holder (e.g. the dimension
        // player's score map) releases first.
        if (score != nullptr) score->AddRef();
        if (score_ != nullptr && !initialized_) score_->Release();  // the original leaks it
        score_ = score;
    }
    dataDir_ = dataDir;
    return InitializeFromLoadedScore();
}

// cFreshPlayer::Init(cGZPersistResourceKey const&) @ 0x1004cd592
bool cFreshPlayer::InitializeFromResourceKey(const cGZPersistResourceKey& key) {
    SetKey(key);
    Fresh::cIFreshHost* host = Fresh::GetHost();
    std::string text;
    if (host == nullptr || !host->ReadScoreText(key, text)) return false;
    auto* score = cFreshScore::Create();
    cRZString textString(text.c_str());
    score->ParseScore(textString);
    score->AddRef();  // the player's own reference (see InitializeWithScoreAndDataDir)
    if (score_ != nullptr && !initialized_) score_->Release();
    score_ = score;
    return InitializeFromLoadedScore();
}

// cFreshPlayer::LoadScore(cIGZString const&, unsigned int) @ 0x1004cf6fa
bool cFreshPlayer::LoadScore(const cIGZString& fileName, std::uint32_t) {
    return InitializeFromPath(cRZString(fileName));
}

// cFreshPlayer::Init() @ 0x1004cd7a6 — builds a cell player for every score cell.
bool cFreshPlayer::InitializeFromLoadedScore() {
    SectionGuard guard(Section());
    if (!initialized_ && score_ != nullptr) {
        lastSubtractedRow_ = lastSubtractedColumn_ = -1;
        lastAddedRow_ = lastAddedColumn_ = -1;
        const auto& h = score_->header;
        volume_ = h.volumeScale;
        xPos_ = yPos_ = 1;
        zPos_ = 1;
        cachedMinCellsPercentWeight_ = h.minCellsPercentWeight;
        cachedMaxCellsAtFullPercent_ = h.maxCellsAtFullPercent;
        columnRadius_ = h.columnRadius;
        rowRadius_ = h.rowRadius;
        for (int row = 0; row < h.rows && row < cFreshScore::kMaxRows; ++row) {
            for (int col = 0; col < h.columns && col < cFreshScore::kMaxColumns; ++col) {
                if (cIFreshCell* cell = score_->GetCell(static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(col), 0)) {
                    auto* cp = new cFreshCellPlayer(this);
                    cp->SetCell(cell);
                    grid_[row][col] = cp;
                }
            }
        }
        activeCells_ = 0;
        for (int row = 0; row < h.rows; ++row) {
            for (int col = 0; col < h.columns; ++col) {
                if (CellIsInSelectionArea(row, col)) ++activeCells_;
            }
        }
        initialized_ = true;
    }
    return true;
}

void cFreshPlayer::DestroyCellPlayersAndScore() {
    if (score_ != nullptr) {
        score_->Release();  // the original calls the deleting destructor directly
        score_ = nullptr;
    }
    for (auto& row : grid_) {
        for (auto& cp : row) {
            delete cp;
            cp = nullptr;
        }
    }
}

// cFreshPlayer::Shutdown() @ 0x1004cd940
bool cFreshPlayer::Shutdown() {
    StopFade();
    SectionGuard guard(Section());
    if (initialized_) {
        Stop();
        DestroyCellPlayersAndScore();
    }
    playing_ = false;
    initialized_ = false;
    if (external_ != nullptr) {
        cIExternalSoundSystem* e = external_;
        external_ = nullptr;
        e->Release();
    }
    return true;
}

void cFreshPlayer::SetExternalSoundSystemInterface(cIExternalSoundSystem* system) {
    cIExternalSoundSystem* old = external_;
    if (old == system) return;
    if (system != nullptr) system->AddRef();
    external_ = system;
    if (old != nullptr) old->Release();
}

// ---- transport ------------------------------------------------------------------------------

// cFreshPlayer::Play() @ 0x1004cda5e
bool cFreshPlayer::Play() {
    SectionGuard guard(Section());
    if (initialized_) {
        if (fading_) StopFade();
        if (!playing_) {
            manual_ = false;
            const auto& h = score_->header;
            tempo_ = h.tempo;
            beatOffset_ = h.beatsPerCycle;
            if (h.beatsPerCycle > 2) beatOffset_ = h.beatsPerCycle - 1;
            beat_ = beatOffset_;
            beatTotal_ = beatOffset_;
            UpdatePlayerInfo();
            timer_ = std::make_unique<cRZTimer>(cRZTimer::kMilliseconds);
            timer_->Start();
            SetXPos(h.initialXPosition);
            SetYPos(h.initialYPosition);
            playing_ = true;
            Fresh::cIFreshHost* host = Fresh::GetHost();
            if (host != nullptr && host->HasSoundSystem()) {
                host->AddUpdateTarget(static_cast<cIGZSndUpdateTarget*>(this), 250);
            }
        }
    }
    return true;
}

// cFreshPlayer::Play(int) @ 0x1004cda40
bool cFreshPlayer::PlayAtVolume(std::int32_t volume) {
    SetVolume(volume);
    return Play();
}

// cFreshPlayer::Stop() @ 0x1004cdcaa
bool cFreshPlayer::Stop() {
    SectionGuard guard(Section());
    return StopNoLock();
}

// cFreshPlayer::StopNoLock() @ 0x1004cdd10
bool cFreshPlayer::StopNoLock() {
    if (initialized_ && playing_) {
        Fresh::cIFreshHost* host = Fresh::GetHost();
        if (host != nullptr && host->HasSoundSystem()) {
            host->RemoveUpdateTarget(static_cast<cIGZSndUpdateTarget*>(this));
        }
        StopFade();
        if (timer_) {
            timer_->Stop();
            timer_.reset();
        }
        ForEachCellPlayer([](int, int, cFreshCellPlayer& cp) {
            if (cp.IsBusy()) cp.Stop();
        });
        playing_ = false;
    }
    return true;
}

// cFreshPlayer::SetTempo(int) @ 0x1004cf118
bool cFreshPlayer::SetTempo(std::int32_t beatsPerMinute) {
    SectionGuard guard(Section());
    if (IsPlaying() && tempo_ != beatsPerMinute) {
        tempo_ = beatsPerMinute;
        timer_->Stop();
        timer_->Restart();
        beatTotal_ = beatOffset_ + beat_;
    }
    return true;
}

// cFreshPlayer::Reseed(unsigned int) @ 0x1004cf5c0
bool cFreshPlayer::Reseed(std::uint32_t seed) {
    random_->Seed(seed);
    beat_ = 0;
    beatTotal_ = 1;
    beatOffset_ = 0;
    cachedMinCellsPercentWeight_ = 0;
    cachedMaxCellsAtFullPercent_ = 4;
    lastAddedRow_ = lastAddedColumn_ = 0;
    lastSubtractedRow_ = lastSubtractedColumn_ = 0;
    UpdatePlayerInfo();
    if (playing_) {
        Stop();
        Play();
    }
    return true;
}

// ---- volume ---------------------------------------------------------------------------------

// cFreshPlayer::SetVolume(int) @ 0x1004cedba
bool cFreshPlayer::SetVolume(std::int32_t volume) {
    SectionGuard guard(Section());
    return SetVolumeNoLock(volume);
}

// cFreshPlayer::SetVolumeNoLock(int) @ 0x1004cee26
bool cFreshPlayer::SetVolumeNoLock(std::int32_t volume) {
    if (initialized_) {
        volume_ = volume;
        ForEachCellPlayer([this](int, int, cFreshCellPlayer& cp) { cp.SetVol(volume_); });
    }
    return true;
}

// cFreshPlayer::StopFade() @ 0x1004cde0e
void cFreshPlayer::StopFade() {
    if (!fading_) return;
    fadeTimer_->Stop();
    {
        SectionGuard guard(Section());
        fading_ = false;
    }
    fadeTimer_->Stop();
    fadeTimer_.reset();
}

void cFreshPlayer::FadeTimerThunk(void* player) { static_cast<cFreshPlayer*>(player)->ProcessVolumeFadeTimerCallback(); }

// cFreshPlayer::FadeVolume(int, int, bool) @ 0x1004cef92 — twenty steps of (target-volume)/20,
// one every duration/20 ms; differences under 20 are applied immediately.
bool cFreshPlayer::FadeVolume(std::int32_t durationMs, std::int32_t targetVolume, bool stopAtEnd) {
    SectionGuard guard(Section());
    if (initialized_ && playing_) {
        stopAfterFade_ = stopAtEnd;
        fadeTarget_ = targetVolume;
        const std::int32_t delta = targetVolume - volume_;
        fadeStep_ = delta / 20;
        if (static_cast<std::uint32_t>(delta + 19) < 39u) {
            SetVolume(targetVolume);
            if (stopAtEnd) Stop();
        } else if (!fading_) {
            fadeTimer_ = std::make_unique<cRZCallbackTimer>();
            fadeTimer_->SetCallbackFunction(&cFreshPlayer::FadeTimerThunk, this);
            fadeTimer_->SetResolution(100);
            fadeTimer_->StartPeriodic(static_cast<std::uint32_t>(durationMs / 20), nullptr);
            fading_ = true;
        }
    }
    return true;
}

// cFreshPlayer::ProcessVolumeFadeTimerCallback() @ 0x1004ceea6
void cFreshPlayer::ProcessVolumeFadeTimerCallback() {
    if (Section().IsLocked() != 0) return;  // somebody else holds the lock: skip this tick
    SectionGuard guard(Section());
    if (!fading_) return;
    std::int32_t v = fadeStep_ + volume_;
    if (v < 0) v = 0;
    v = std::min(v, kMaxVolume);
    SetVolumeNoLock(v);
    if (fadeStep_ < 0) {
        if (v <= fadeTarget_) {
            StopFade();
            if (stopAfterFade_) StopNoLock();
        }
    } else if (fadeStep_ != 0 && fadeTarget_ <= v) {
        StopFade();
    }
}

// ---- position / selection area ---------------------------------------------------------------

namespace {
// Shared by SetXPos/SetYPos: scale 0..1000 to a cell index, snap to the selection area in
// discrete mode and keep the whole area inside the grid.
std::int32_t ScaleAndClampPosition(std::int32_t position, std::int32_t extent, std::int32_t radius, bool discrete) {
    std::int32_t v = (position * extent) / 1000;
    if (discrete) v = (v + radius) - v % (radius * 2 + 1);
    std::int32_t t = (v < extent - radius) ? v : (extent - radius) - 1;
    if (t < radius) t = radius;
    if (t < 0) t = 0;
    return t < extent ? t : extent - 1;
}
}  // namespace

// cFreshPlayer::SetXPos(int) @ 0x1004cf1b6
bool cFreshPlayer::SetXPos(std::int32_t position) {
    if (initialized_) {
        xPos_ = ScaleAndClampPosition(position, score_->header.columns, columnRadius_, score_->header.discreteMode != 0);
        UpdatePlayerInfo();
    }
    return true;
}

// cFreshPlayer::SetYPos(int) @ 0x1004cf22a
bool cFreshPlayer::SetYPos(std::int32_t position) {
    if (initialized_) {
        yPos_ = ScaleAndClampPosition(position, score_->header.rows, rowRadius_, score_->header.discreteMode != 0);
        UpdatePlayerInfo();
    }
    return true;
}

bool cFreshPlayer::SetZoom(std::int32_t zoom) { if (initialized_) zoom_ = zoom; return true; }          // @ 0x1004cf2a6
bool cFreshPlayer::SetPlayPercent(std::int32_t p) { if (initialized_) playPercent_ = p; return true; }  // @ 0x1004cf2b8
bool cFreshPlayer::SetManual(bool manual) { if (initialized_) manual_ = manual; return true; }          // @ 0x1004cf2cc

bool cFreshPlayer::SetSelectionAreaMaxDistances(std::int32_t rowRadius, std::int32_t columnRadius) {
    rowRadius_ = rowRadius;
    columnRadius_ = columnRadius;
    return true;
}
bool cFreshPlayer::GetSelectionAreaMaxDistances(std::int32_t& rowRadius, std::int32_t& columnRadius) {
    rowRadius = rowRadius_;
    columnRadius = columnRadius_;
    return true;
}

std::int32_t cFreshPlayer::GetFirstSelectableRow() { return std::max(0, yPos_ - rowRadius_); }
std::int32_t cFreshPlayer::GetLastSelectableRow() {
    return rowRadius_ + yPos_ < score_->header.rows ? rowRadius_ + yPos_ : score_->header.rows - 1;
}
std::int32_t cFreshPlayer::GetFirstSelectableColumn() { return std::max(0, xPos_ - columnRadius_); }
std::int32_t cFreshPlayer::GetLastSelectableColumn() {
    return columnRadius_ + xPos_ < score_->header.columns ? columnRadius_ + xPos_ : score_->header.columns - 1;
}

// cFreshPlayer::CellIsInSelectionArea(int, int) @ 0x1004ce676
bool cFreshPlayer::CellIsInSelectionArea(std::int32_t row, std::int32_t column) {
    return std::abs(row - yPos_) <= rowRadius_ && std::abs(column - xPos_) <= columnRadius_;
}

cIFreshCellPlayer* cFreshPlayer::GetCellPlayer(std::uint32_t row, std::uint32_t column, std::uint32_t) {
    return row < cFreshScore::kMaxRows && column < cFreshScore::kMaxColumns ? grid_[row][column] : nullptr;
}

// cFreshPlayer::UpdatePlayerInfo() @ 0x1004cdb8e — recounts the score cells in the selection area:
// activeCells_ = all of them, groupCellCount_ = ungrouped cells + one per distinct group id.
void cFreshPlayer::UpdatePlayerInfo() {
    std::array<bool, 256> seenGroup{};
    activeCells_ = 0;
    groupCellCount_ = 0;
    const std::int32_t firstRow = GetFirstSelectableRow(), lastRow = GetLastSelectableRow();
    for (std::int32_t row = firstRow; row <= lastRow; ++row) {
        const std::int32_t firstCol = GetFirstSelectableColumn();
        for (std::int32_t col = firstCol; col <= GetLastSelectableColumn(); ++col) {
            auto* cell = static_cast<cFreshCell*>(score_->GetCell(static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(col), 0));
            if (cell == nullptr) continue;
            const std::uint32_t group = cell->GetGroupId();
            if (group == 0) {
                ++groupCellCount_;
            } else if (group >= seenGroup.size() || !seenGroup[group]) {
                if (group < seenGroup.size()) seenGroup[group] = true;
                ++groupCellCount_;
            }
            ++activeCells_;
        }
    }
}

// ---- cell bookkeeping -------------------------------------------------------------------------

std::int32_t cFreshPlayer::NumCellsBusy() {  // @ 0x1004cf2de
    std::int32_t n = 0;
    ForEachCellPlayer([&n](int, int, cFreshCellPlayer& cp) { n += cp.IsBusy() ? 1 : 0; });
    return n;
}
std::int32_t cFreshPlayer::NumCellsDying() {  // @ 0x1004cf35e
    std::int32_t n = 0;
    ForEachCellPlayer([&n](int, int, cFreshCellPlayer& cp) { n += cp.markedForRemoval() ? 1 : 0; });
    return n;
}
std::int32_t cFreshPlayer::NumCellsPlaying() {  // @ 0x1004cf3b2
    std::int32_t n = 0;
    ForEachCellPlayer([&n](int, int, cFreshCellPlayer& cp) { n += cp.IsPlaying() ? 1 : 0; });
    return n;
}

// cFreshPlayer::ShowUsage() @ 0x1004cf432 — the usage numbers are computed and discarded.
bool cFreshPlayer::ShowUsage() {
    if (initialized_) {
        if (!playing_) return false;
        NumCellsBusy();
        NumCellsPlaying();
    }
    return true;
}

// cFreshPlayer::GetMinCellsPlaying() @ 0x1004cf4be
std::int32_t cFreshPlayer::GetMinCellsPlaying() {
    const auto& h = score_->header;
    std::int32_t colSpan = h.columns - 1;
    if (colSpan == 0) colSpan = 1;
    std::int32_t rowSpan = h.rows - 1;
    if (rowSpan == 0) rowSpan = 1;
    std::int32_t v = ((xPos_ + 1) * h.minCellsColumnWeight) / colSpan +
                     (playPercent_ * h.minCellsPercentWeight) / 100 +
                     ((yPos_ + 1) * h.minCellsRowWeight) / rowSpan;
    if (groupCellCount_ < v) v = groupCellCount_;
    return v;
}

// cFreshPlayer::GetMaxCellsPlaying() @ 0x1004cf530
std::int32_t cFreshPlayer::GetMaxCellsPlaying() {
    return (playPercent_ * score_->header.maxCellsAtFullPercent) / 100;
}

// cFreshPlayer::ProbOfCellStarting(int, int, int) @ 0x1004ce462
// Score of starting the cell at (row, column): 0 = not eligible, 100/101 = unconditional (101 when
// it belongs to the preferred group), 2 = chance-based but in the preferred group, otherwise
// 1 or 0 from a rand() draw against alCellProbMultiplier[probability]/activeCells.
std::int32_t cFreshPlayer::ProbOfCellStarting(std::int32_t row, std::int32_t column, std::int32_t preferredGroup) {
    auto* cell = static_cast<cFreshCell*>(score_->GetCell(static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column), 0));
    cFreshCellPlayer* cp = grid_[row][column];
    if (cell == nullptr || cp == nullptr) return 0;
    if (cp->IsDead() || cp->IsBusy() || !CellIsInSelectionArea(row, column)) return 0;
    if (lastSubtractedRow_ == row && lastSubtractedColumn_ == column) return 0;

    const std::uint32_t group = cell->GetGroupId();
    if (group != 0) {  // another busy, not-dying cell of the same group blocks this one
        bool blocked = false;
        ForEachCellPlayer([&](int, int, cFreshCellPlayer& other) {
            auto* oc = static_cast<cFreshCell*>(other.GetCell());
            if (oc != nullptr && oc->GetGroupId() == group && other.IsBusy() && !other.markedForRemoval()) blocked = true;
        });
        if (blocked) return 0;
    }
    for (const cRZPoint& p : cell->connections()) {  // connected cells that are playing also block
        if (p.y < score_->header.rows && p.x < score_->header.columns) {
            if (cFreshCellPlayer* other = grid_[p.y][p.x]) {
                if (other->IsBusy() && !other->markedForRemoval()) return 0;
            }
        }
    }

    std::int32_t probability = cell->GetProbability();
    if (probability > 20) {
        cell->SetProbability(20);
    } else {
        if (probability < 0) {
            cell->SetProbability(0);
            return 0;
        }
        if (probability == 0) return 0;
        if (probability != 20) {
            std::int32_t active = activeCells_;
            if (active == 0) active = activeCells_ = 10;
            if (preferredGroup != 0 && static_cast<std::int32_t>(group) == preferredGroup) return 2;
            const std::int32_t threshold = Fresh::kCellProbabilityMultiplier[static_cast<std::size_t>(probability)] / active;
            return std::rand() < threshold ? 1 : 0;
        }
    }
    return (preferredGroup != 0 && static_cast<std::int32_t>(group) == preferredGroup) ? 101 : 100;
}

// cFreshPlayer::GetCellToAdd(int&, int&, int) @ 0x1004ce6a0 — evaluates every cell of the selection
// area once, starting from a random cell and wrapping around; keeps the first highest score.
bool cFreshPlayer::GetCellToAdd(std::int32_t& row, std::int32_t& column, std::int32_t preferredGroup) {
    const std::uint32_t r = random_->RandomUint32Uniform();
    const std::int32_t firstRow = GetFirstSelectableRow(), firstCol = GetFirstSelectableColumn();
    const std::int32_t lastRow = GetLastSelectableRow(), lastCol = GetLastSelectableColumn();
    std::int32_t startRow = (yPos_ - rowRadius_) + static_cast<std::int32_t>(((r >> 8) & 0xff) % static_cast<std::uint32_t>(rowRadius_ * 2 + 1));
    std::int32_t startCol = (xPos_ - columnRadius_) + static_cast<std::int32_t>((r & 0xff) % static_cast<std::uint32_t>(columnRadius_ * 2 + 1));
    startRow = std::min(std::max(startRow, 0), score_->header.rows - 1);
    startCol = std::min(std::max(startCol, 0), score_->header.columns - 1);

    std::int32_t best = 0;
    auto visit = [&](std::int32_t rr, std::int32_t cc) {
        const std::int32_t p = ProbOfCellStarting(rr, cc, preferredGroup);
        if (best < p) {
            row = rr;
            column = cc;
            best = p;
        }
    };
    auto scanRow = [&](std::int32_t rr) {
        for (std::int32_t cc = startCol; cc <= lastCol; ++cc) visit(rr, cc);
        for (std::int32_t cc = firstCol; cc < startCol; ++cc) visit(rr, cc);
    };
    for (std::int32_t rr = startRow; rr <= lastRow; ++rr) scanRow(rr);
    for (std::int32_t rr = firstRow; rr < startRow; ++rr) scanRow(rr);
    return best != 0;
}

// cFreshPlayer::GetCellToSubtract(int&, int&) @ 0x1004ce9aa
// Prefers a playing cell OUTSIDE the selection area (scan from a random cell, wrapping); failing
// that picks a random playing cell inside it whose probability is not 20. Cells already marked for
// removal are skipped. (The original dereferences missing cell players; those are skipped here.)
bool cFreshPlayer::GetCellToSubtract(std::int32_t& row, std::int32_t& column) {
    const std::uint32_t r = random_->RandomUint32Uniform();
    const std::int32_t rows = score_->header.rows, cols = score_->header.columns;
    const std::int32_t startRow = static_cast<std::int32_t>(((r >> 8) & 0xff) % static_cast<std::uint32_t>(rows));
    const std::int32_t startCol = static_cast<std::int32_t>((r & 0xff) % static_cast<std::uint32_t>(cols));

    auto eligible = [&](std::int32_t rr, std::int32_t cc) {
        cFreshCellPlayer* cp = grid_[rr][cc];
        return cp != nullptr && cp->IsPlaying() && !CellIsInSelectionArea(rr, cc) && !cp->markedForRemoval();
    };
    auto scanRow = [&](std::int32_t rr) {
        for (std::int32_t cc = startCol; cc < cols; ++cc) {
            if (eligible(rr, cc)) { row = rr; column = cc; return true; }
        }
        for (std::int32_t cc = 0; cc < startCol; ++cc) {
            if (eligible(rr, cc)) { row = rr; column = cc; return true; }
        }
        return false;
    };
    for (std::int32_t rr = startRow; rr < rows; ++rr) if (scanRow(rr)) return true;
    for (std::int32_t rr = 0; rr < startRow; ++rr) if (scanRow(rr)) return true;

    // nothing outside the area: choose among removable cells inside it
    std::vector<std::pair<std::int32_t, std::int32_t>> candidates;
    const std::int32_t firstRow = std::max(0, yPos_ - rowRadius_), firstCol = std::max(0, xPos_ - columnRadius_);
    const std::int32_t lastRow = std::min(rows - 1, yPos_ + rowRadius_), lastCol = std::min(cols - 1, xPos_ + columnRadius_);
    for (std::int32_t rr = firstRow; rr <= lastRow; ++rr) {
        for (std::int32_t cc = firstCol; cc <= lastCol; ++cc) {
            cFreshCellPlayer* cp = grid_[rr][cc];
            if (cp != nullptr && cp->IsPlaying() && !cp->markedForRemoval() &&
                static_cast<cFreshCell*>(cp->GetCell())->GetProbability() != 20) {
                candidates.emplace_back(rr, cc);
            }
        }
    }
    if (candidates.empty()) return false;
    const auto pick = random_->RandomUint32Uniform() % static_cast<std::uint32_t>(candidates.size());
    row = candidates[pick].first;
    column = candidates[pick].second;
    return true;
}

// cFreshPlayer::SubtractCell(int, int) @ 0x1004ced66
void cFreshPlayer::SubtractCell(std::int32_t row, std::int32_t column) {
    lastSubtractedRow_ = row;
    lastSubtractedColumn_ = column;
    if (cFreshCellPlayer* cp = grid_[row][column]) {
        if (cp->IsBusy()) cp->Stop();
    }
}

// cFreshPlayer::ToggleCell(int, int) @ 0x1004ce90e
bool cFreshPlayer::ToggleCell(std::int32_t row, std::int32_t column) {
    cFreshCellPlayer* cp = grid_[row][column];
    if (cp == nullptr) return true;
    if (!cp->IsBusy()) {
        if (CalculateCellplayer3DPosition(cp)) {
            cp->SetCell(score_->GetCell(static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column), 0));
            cp->Start();
        }
    } else {
        SubtractCell(row, column);
    }
    return true;
}

// ---- 3D placement --------------------------------------------------------------------------------

bool cFreshPlayer::SetNumDivisionsX(std::uint32_t count) {  // @ 0x1004cf676
    if (count < 3) numDivisionsX_ = count;
    return count < 3;
}
bool cFreshPlayer::SetNumDivisionsZ(std::uint32_t count) {  // @ 0x1004cf68e
    if (count < 3) numDivisionsZ_ = count;
    return count < 3;
}
bool cFreshPlayer::SetDivisionEnabled(std::uint32_t x, std::uint32_t z, bool enabled) {  // @ 0x1004cf618
    if (x >= 3 || z >= 3) return false;
    divisionEnabled_[x][z] = enabled;
    return true;
}
bool cFreshPlayer::SetDivision3DPosition(std::uint32_t x, std::uint32_t z, std::int32_t px, std::int32_t py, std::int32_t pz) {  // @ 0x1004cf63e
    if (x >= 3 || z >= 3) return false;
    divisionPosX_[x][z] = px;
    divisionPosY_[x][z] = py;
    divisionPosZ_[x][z] = pz;
    return true;
}

// cFreshPlayer::CalculateCellplayer3DPosition(cFreshCellPlayer*) @ 0x1004ce2e0
// Picks one of the enabled divisions at random (each weighted 1000; the division used last time
// is excluded and, if the draw lands past all others, simply reused) and positions the cell there.
bool cFreshPlayer::CalculateCellplayer3DPosition(cFreshCellPlayer* cellPlayer) {
    std::int32_t enabledCount = 0;
    for (std::uint32_t x = 0; x <= numDivisionsX_; ++x)
        for (std::uint32_t z = 0; z <= numDivisionsZ_; ++z) enabledCount += divisionEnabled_[x][z] ? 1 : 0;
    if (enabledCount == 0) return false;

    std::uint32_t chosenX = lastDivisionX_, chosenZ = lastDivisionZ_;
    std::uint32_t draw = random_->RandomUint32Uniform(static_cast<std::uint32_t>(enabledCount) * 1000u);
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

// ---- the beat loop --------------------------------------------------------------------------------

// cFreshPlayer::Update(int) @ 0x1004cde7c — advances the beat clock and, on the beats where the
// score may change (beatsPerCycle-1 and beatsPerCycle-6), retires and adds cells.
void cFreshPlayer::Update(std::int32_t) {
    const std::int32_t minCells = GetMinCellsPlaying();
    const std::int32_t maxCells = GetMaxCellsPlaying();
    if (!(initialized_ && playing_ && !paused_)) return;
    SectionGuard guard(Section());

    const std::uint32_t now = timer_->GetElapsedTimeU32();
    const auto& h = score_->header;
    std::int32_t row = 0, column = 0;
    while (true) {
        const std::uint32_t nextBeatTime =
            tempo_ == 0 ? 0xFFFFFFFFu : static_cast<std::uint32_t>(((beatTotal_ - beatOffset_) * 60000) / tempo_);
        if (now < nextBeatTime) return;  // not time for the next beat yet
        if (now > 86400000u) break;      // clock ran for a day: re-base below

        std::int32_t removedProbability = 0;
        std::int32_t removedGroup = 0;
        const bool decisionBeat = beat_ == h.beatsPerCycle - 1 || beat_ == h.beatsPerCycle - 6;
        if (decisionBeat && minCells <= NumCellsPlaying() && !manual_ && h.discreteMode != 0 &&
            GetCellToSubtract(row, column)) {
            cFreshCellPlayer* cp = grid_[row][column];
            cp->SetMarkedForRemoval(true);
            removedGroup = static_cast<std::int32_t>(static_cast<cFreshCell*>(cp->GetCell())->GetGroupId());
            removedProbability = removedGroup == 0 ? 0 : static_cast<cFreshCell*>(cp->GetCell())->GetProbability();
        }

        const std::int32_t busy = NumCellsBusy();
        const std::int32_t dying = NumCellsDying();
        if (decisionBeat && busy - dying < maxCells && playPercent_ != 0 && !fading_ && !manual_) {
            bool ok = GetCellToAdd(row, column, removedGroup);
            if (removedProbability == 0x13 && removedGroup != 0 && !ok) ok = GetCellToAdd(row, column, removedGroup);
            if (removedProbability == 0x13 && removedGroup != 0 && !ok) ok = GetCellToAdd(row, column, removedGroup);
            if (busy - dying < minCells && !ok) ok = GetCellToAdd(row, column, removedGroup);
            if (ok) {
                cFreshCellPlayer* cp = grid_[row][column];
                if (CalculateCellplayer3DPosition(cp)) {
                    cp->SetCell(score_->GetCell(static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column), 0));
                    cp->SetExternalSoundSystemInterface(external_);
                    cp->Start();
                    lastAddedRow_ = row;
                    lastAddedColumn_ = column;
                }
            }
        }

        ForEachCellPlayer([](int, int, cFreshCellPlayer& cp) { cp.Update(); });

        const std::int32_t previousBeat = beat_;
        beat_ = previousBeat + 1;
        if (h.beatsPerCycle <= previousBeat) beat_ = 1;
        ++beatTotal_;
    }
    // re-base the beat clock after a day of continuous play
    beatOffset_ = beat_ = beatTotal_ = h.beatsPerCycle;
    timer_->Restart();
}
