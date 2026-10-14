// Freshness engine: score parsing -> cFreshPlayer -> cFreshCellPlayer -> fake sound system.
#include <cassert>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "audio/FreshDimensionPlayer.h"
#include "audio/FreshHost.h"
#include "audio/FreshPlayer.h"
#include "audio/FreshScore.h"

namespace {

// Minimal sound: tracks volume/playing only; QueryInterface answers nothing but IUnknown/IGZSnd.
class FakeSnd final : public cIGZSnd {
public:
    explicit FakeSnd(std::vector<FakeSnd*>& live) : live_(live) { live_.push_back(this); }
    ~FakeSnd() { std::erase(live_, this); }
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZSnd) { *out = this; AddRef(); return true; }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override { if (--refs_ == 0) { delete this; return 0; } return refs_; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool PlayWithVolumeAndLoop(std::int32_t v, bool, SndDup) override { volume_ = v; playing_ = true; return true; }
    bool PlayWithScriptArguments(std::int32_t, std::int32_t, std::int32_t, std::int32_t) override { return true; }
    bool IsPlaying() const override { return playing_; }
    bool Stop() override { playing_ = false; return true; }
    bool Kill() override { playing_ = false; return true; }
    bool StopAllInstances() override { return true; }
    std::uint32_t GetUseCount() const override { return 0; }
    std::uint32_t GetID() const override { return id_; }
    void SetID(std::uint32_t id) override { id_ = id; }
    bool Load() override { return true; }
    bool Unload() override { return true; }
    bool IsLoaded() const override { return true; }
    bool Pause(bool) override { return true; }
    bool Unpause() override { return true; }
    bool IsPaused() const override { return false; }
    std::int32_t GetVolume() const override { return volume_; }
    bool SetVolume(std::int32_t v) override { volume_ = v; return true; }
    bool FadeVolume(std::int32_t, std::int32_t to, std::uint32_t, bool) override { volume_ = to; return true; }
    bool IsFading() const override { return false; }
    std::int32_t GetPan() const override { return 0; }
    bool SetPan(std::int32_t) override { return true; }
    std::int32_t GetFrequency() const override { return 22050; }
    bool SetFrequency(std::int32_t) override { return true; }
    std::int32_t GetOriginalFrequency() const override { return 22050; }
    bool SetEffectsType(std::int32_t, std::uint32_t) override { return true; }
    bool SetEffectsLevel(std::int32_t, std::uint32_t) override { return true; }
    std::int32_t GetSoundPriority() const override { return 0; }
    bool SetSoundPriority(std::int32_t) override { return true; }
    bool SetName(const cIGZString&) override { return true; }
    bool GetName(cIGZString&) const override { return false; }
    bool SetSoundFileFromString(const cIGZString&, SndStreamingType) override { return true; }
    bool SetSoundFileFromChars(const char*, std::uint32_t, SndStreamingType) override { return true; }

private:
    std::vector<FakeSnd*>& live_;
    std::uint32_t refs_ = 1;
    std::uint32_t id_ = 0;
    std::int32_t volume_ = 0;
    bool playing_ = false;
};

class FakeHost final : public Fresh::cIFreshHost {
public:
    bool HasSoundSystem() const override { return true; }
    bool IsSoundSystemInTestMode() const override { return testMode; }
    cIFreshPlayer* CreateFreshPlayerInstance() override { auto* p = cFreshPlayer::Create(); p->AddRef(); return p; }
    cIGZSnd* CreateSndByFileName(const cIGZString& f) override {
        names.insert(f.ToChar());
        return new FakeSnd(live);
    }
    cIGZSnd* CreateSndByResourceId(std::uint32_t id) override { ids.push_back(id); return new FakeSnd(live); }
    void AddUpdateTarget(cIGZSndUpdateTarget*, std::uint32_t) override { ++adds; }
    void RemoveUpdateTarget(cIGZSndUpdateTarget*) override { ++removes; }
    bool ReadScoreText(const cGZPersistResourceKey&, std::string& out) override { out = text; return !text.empty(); }

    std::vector<FakeSnd*> live;
    std::set<std::string> names;
    std::vector<std::uint32_t> ids;
    int adds = 0, removes = 0;
    bool testMode = false;
    std::string text;
};

// 3x3 score, all cells always-start (probability 20), version 7.
std::string MakeScore(bool discrete) {
    std::string s = "version\n7\nheader\n";
    s += "\t1024\t" + std::string(discrete ? "1" : "0") + "\t0\t3\t3\t3\t120\t8\t1\t1\t1\t1\t0\t0\n";
    s += "cells\n";
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            // vol rv panX panZ rp fi fo sd dr loopLen looped quant prob pLo pHi fast group stereo name
            s += "\t1024\t0\t512\t512\t0\t0\t0\t0\t0\t8\t1\t0\t20\t0\t0\t0\t0\t0\tcell" + std::to_string(r) + std::to_string(c) + ".wav\n";
        }
    }
    return s;
}

}  // namespace

int main() {
    FakeHost host;
    Fresh::SetHost(&host);

    // --- score parsing ---
    cFreshScore* score = cFreshScore::Create();
    cRZString text(MakeScore(true).c_str());
    assert(score->ParseScore(text));
    assert(score->header.rows == 3 && score->header.columns == 3 && score->header.tempo == 120);
    assert(score->header.beatsPerCycle == 8 && score->header.columnRadius == 1 && score->header.rowRadius == 1);
    assert(score->GetCell(0, 0, 0) != nullptr && score->GetCell(3, 0, 0) == nullptr);
    assert(score->GetCell(2, 2, 0)->GetProbability() == 20);

    // --- player init ---
    cFreshPlayer* player = cFreshPlayer::Create();
    player->AddRef();
    void* p = nullptr;
    assert(!player->QueryInterface(kGZIID_cIGZUnknown, &p));   // the original rejects IUnknown's id
    assert(player->InitializeWithScoreAndDataDir(score, cRZString("")));
    assert(player->GetScore(0) == score);
    assert(player->cellPlayerAt(0, 0) != nullptr && player->cellPlayerAt(2, 2) != nullptr);
    assert(player->cellPlayerAt(3, 3) == nullptr);

    // selection area: radius 1 around (xPos,yPos); position (0..1000) scaled to the 3x3 grid
    player->SetXPos(500);
    player->SetYPos(500);
    assert(player->GetXPos() == 1 && player->GetYPos() == 1);
    assert(player->GetFirstSelectableRow() == 0 && player->GetLastSelectableRow() == 2);
    player->SetXPos(0);
    assert(player->GetXPos() == 1);  // clamped so the area stays inside the grid
    assert(player->CellIsInSelectionArea(0, 2) && !player->CellIsInSelectionArea(0, 3));
    assert(player->activeCellCount() == 9);

    // --- ProbOfCellStarting: probability 20 is unconditional ---
    assert(player->ProbOfCellStarting(0, 0, 0) == 100);
    assert(player->ProbOfCellStarting(0, 0, 5) == 100);   // group 0 never matches the preferred group

    // --- play and run beats ---
    assert(player->Play() && player->IsPlaying());
    assert(host.adds == 1);
    player->SetVolume(512);
    assert(player->MasterVolume() == 512);

    // manual cells: toggling starts / stops a specific cell
    assert(player->ToggleCell(1, 1));
    assert(player->NumCellsBusy() == 1 && player->NumCellsPlaying() == 1);
    assert(host.names.count("cell11.wav") == 1);
    assert(host.live.size() == 1);
    assert(player->ToggleCell(1, 1));            // busy -> stop (no fade-out: killed at once)
    assert(player->NumCellsBusy() == 0 && host.live.empty());
    // the cell just stopped may not be restarted by the automatic chooser
    assert(player->ProbOfCellStarting(1, 1, 0) == 0);

    std::int32_t r = -1, c = -1;
    assert(player->GetCellToAdd(r, c, 0));
    assert(r >= 0 && r <= 2 && c >= 0 && c <= 2 && !(r == 1 && c == 1));
    assert(!player->GetCellToSubtract(r, c));    // nothing is playing

    // min/max cell targets: max = playPercent(100) * 3 / 100
    assert(player->GetMaxCellsPlaying() == 3);
    assert(player->GetMinCellsPlaying() == 0);

    // --- fade: sub-20 differences are applied immediately, stop-at-end stops ---
    player->SetVolume(1000);
    assert(player->FadeVolume(100, 1010, false));
    assert(player->MasterVolume() == 1010);
    assert(player->FadeVolume(100, 1010, true));
    assert(!player->IsPlaying());
    assert(host.removes >= 1);

    // --- shutdown releases everything ---
    assert(player->Shutdown());
    assert(player->GetScore(0) == nullptr);
    assert(player->Release() == 0);
    assert(host.live.empty());

    // --- InitializeFromResourceKey goes through the host ---
    host.text = MakeScore(false);
    cFreshPlayer* q = cFreshPlayer::Create();
    q->AddRef();
    cGZPersistResourceKey key;
    assert(q->InitializeFromResourceKey(key));
    assert(q->GetScore(0) != nullptr && q->cellPlayerAt(2, 2) != nullptr);
    assert(q->Release() == 0);

    // --- dimension player: pools the cell players of every enabled score ---
    const auto path = (std::filesystem::temp_directory_path() / "sc4_fresh_dim_test.txt").string();
    { std::ofstream f(path, std::ios::binary); f << MakeScore(false); }
    cIFreshPlayer* dim = cFreshDimensionPlayer::Create();
    dim->AddRef();
    void* dp = nullptr;
    assert(dim->QueryInterface(kGZIID_cIFreshPlayer, &dp) && dp == dim);
    dim->Release();
    assert(dim->QueryInterface(kGZIID_cIFreshDimensionPlayer, &dp));
    auto* dimIface = static_cast<cIFreshDimensionPlayer*>(dp);
    assert(dimIface->AsIFreshPlayer() == dim);
    dimIface->Release();
    assert(!dim->QueryInterface(0x1234, &dp));
    assert(dim->InitializeFromLoadedScore());
    assert(!dim->InitializeFromResourceKey(key) && dim->SetTempo(100) == false);
    cRZString scoreFile(path.c_str());
    assert(dim->LoadScore(scoreFile, 3) && dim->LoadScore(scoreFile, 7));
    assert(dim->GetScore(3) != nullptr && dim->GetScore(7) != nullptr && dim->GetScore(4) == nullptr);
    auto* dimImpl = static_cast<cFreshDimensionPlayer*>(dim);
    assert(dimImpl->pooledCellCount() == 0);   // nothing enabled yet
    dimIface = static_cast<cIFreshDimensionPlayer*>(dimImpl);
    dimIface->SetScoreEnabled(3, true);
    dim->SetXPos(500);
    dim->SetYPos(500);
    assert(dimImpl->enabledScoreCount() == 1);
    // the 3x3 score is non-discrete: every cell of the 9 is in the area (radius 1 around centre)
    assert(dimImpl->pooledCellCount() == 9 && dimImpl->totalProbabilityWeight() == 9 * 20);
    dimIface->SetScoreEnabled(7, true);
    assert(dimImpl->enabledScoreCount() == 2 && dimImpl->pooledCellCount() == 18);
    dimIface->SetScoreEnabled(0, true);        // id 0 is never pooled
    assert(dimImpl->pooledCellCount() == 18);
    assert(dim->SetNumDivisionsX(2) && !dim->SetNumDivisionsX(3));
    assert(!dim->SetDivisionEnabled(3, 0, true) && dim->SetDivisionEnabled(0, 0, true));
    assert(dim->SetVolume(800));
    assert(dim->Play() && dim->IsPlaying());
    dimImpl->Update(0);                         // too early to start a cell: nothing yet
    assert(dimImpl->activeCellPlayerCount() == 0);
    host.testMode = true;
    dimImpl->UpdateCellPlayerMap();            // test mode: the pool is left untouched
    assert(dimImpl->pooledCellCount() == 18);
    host.testMode = false;
    assert(dim->Stop() && !dim->IsPlaying());
    assert(dim->Release() == 0);
    std::filesystem::remove(path);

    Fresh::SetHost(nullptr);
    std::puts("freshplayer ok");
}
