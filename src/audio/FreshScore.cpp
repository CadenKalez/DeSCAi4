#include "audio/FreshScore.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

#include "rz/RZConvert.h"

cFreshScore::~cFreshScore() { ClearGrid(); }

void cFreshScore::ClearGrid() {
    for (auto& row : grid_) {
        for (auto& cell : row) {
            if (cell != nullptr) {
                cell->Release();
                cell = nullptr;
            }
        }
    }
}

bool cFreshScore::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != kGZIID_cIGZUnknown) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cFreshScore::Release() {
    if (refCount_ > 1) return static_cast<std::uint32_t>(--refCount_);
    delete this;
    return 0;
}

namespace {

// One "\t%d ...\t%d\t%s" cell line. `intCount` is 18 for version 7 and 17 for versions 5/6; the
// version-7 extra value is the stereo-sample flag. Field order matches the sscanf argument list.
struct CellFields {
    int volume = 0, volIsRandom = 0, panX = 0, panZ = 0, panIsRandom = 0, fadeIn = 0, fadeOut = 0,
        startDelay = 0, delayIsRandom = 0, loopLength = 0, sampleLooped = 0, quantize = 0,
        probability = 0, pitchLow = 0, pitchHigh = 0, useFastTimer = 0, groupId = 0, stereo = 0;
    char name[256] = {};
};

bool ScanCellLine(const char* line, bool version7, CellFields& f) {
    if (version7) {
        return std::sscanf(line,
                           "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%255s",
                           &f.volume, &f.volIsRandom, &f.panX, &f.panZ, &f.panIsRandom, &f.fadeIn, &f.fadeOut,
                           &f.startDelay, &f.delayIsRandom, &f.loopLength, &f.sampleLooped, &f.quantize,
                           &f.probability, &f.pitchLow, &f.pitchHigh, &f.useFastTimer, &f.groupId, &f.stereo,
                           f.name) >= 0;
    }
    return std::sscanf(line, "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%255s",
                       &f.volume, &f.volIsRandom, &f.panX, &f.panZ, &f.panIsRandom, &f.fadeIn, &f.fadeOut,
                       &f.startDelay, &f.delayIsRandom, &f.loopLength, &f.sampleLooped, &f.quantize,
                       &f.probability, &f.pitchLow, &f.pitchHigh, &f.useFastTimer, &f.groupId, f.name) >= 0;
}

}  // namespace

// cFreshScore::ParseScore(cRZString&) @ 0x1004ca336
bool cFreshScore::ParseScore(cRZString& text) {
    auto nextLine = [&text]() { return text.SplitToken("\n"); };

    if (!text.BeginsWith("version", 7)) return true;
    nextLine();                                   // the "version" label line
    cRZString versionLine = nextLine();           // its value
    const std::uint32_t version = RZ::ConvertToUint(versionLine.ToChar());
    if (version != 7 && version != 5 && version != 6) return true;

    nextLine();                                   // label line
    cRZString headerLine = nextLine();            // 14 tab-separated ints
    Header& h = header;
    std::sscanf(headerLine.ToChar(), "\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                &h.volumeScale, &h.discreteMode, &h.minCellsPercentWeight, &h.maxCellsAtFullPercent,
                &h.rows, &h.columns, &h.tempo, &h.beatsPerCycle, &h.columnRadius, &h.rowRadius,
                &h.headerValue3C, &h.headerValue40, &h.minCellsColumnWeight, &h.minCellsRowWeight);
    nextLine();                                   // label line before the cells

    ClearGrid();
    const bool v7 = version == 7;
    for (int row = 0; row < h.rows && row < kMaxRows; ++row) {
        for (int col = 0; col < h.columns && col < kMaxColumns; ++col) {
            cFreshCell* cell = cFreshCell::Create();
            cRZString line = nextLine();
            CellFields f;
            ScanCellLine(line.ToChar(), v7, f);
            cell->SetVolIsRandom(f.volIsRandom != 0);
            cell->SetPanIsRandom(f.panIsRandom != 0);
            cell->SetDelayIsRandom(f.delayIsRandom != 0);
            cell->SetSampleLooped(f.sampleLooped != 0);
            cell->SetUseFastTimer(f.useFastTimer != 0);
            cell->SetStereoSample(v7 ? f.stereo != 0 : false);
            cell->SetVolume(f.volume);
            cell->SetPanX(f.panX);
            cell->SetPanZ(f.panZ);
            cell->SetFadeInTime(f.fadeIn);
            cell->SetFadeOutTime(f.fadeOut);
            cell->SetStartDelay(f.startDelay);
            cell->SetLoopLength(f.loopLength);
            cell->SetQuantize(f.quantize);
            cell->SetProbability(f.probability);
            cell->SetRandPitchShiftLow(f.pitchLow);
            cell->SetRandPitchShiftHigh(f.pitchHigh);
            cell->SetGroupId(static_cast<std::uint32_t>(f.groupId));

            // Connections: text up to the first '(' is skipped, then "x,y)" groups follow while the
            // separator found before the next '(' is non-empty (exactly the original's loop).
            cRZString lineCopy(line);
            cRZString skipped = lineCopy.SplitToken("(");
            cRZString separator = skipped;
            while (separator.Strlen() != 0) {
                cRZString pair = lineCopy.SplitToken(")");
                cRZPoint p;
                std::sscanf(pair.ToChar(), "%d,%d", &p.x, &p.y);
                cell->connections().push_back(p);
                separator = lineCopy.SplitToken("(");
            }

            // The file name is stored as written; "NONE" (any case) means the cell is absent.
            cRZString name(f.name);
            cell->SetFilename(name);
            cRZString upper(f.name);
            upper.MakeUpper();
            if (upper.IsEqualToChars("NONE", 4, true)) {
                cell->Release();
                cell = nullptr;
            }
            grid_[row][col] = cell;
        }
    }
    return true;
}

// cFreshScore::LoadScore(cRZString) @ 0x1004cb482 (file read via cRZFile in the original)
bool cFreshScore::LoadScoreFromPath(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.empty()) return false;
    cRZString text(data.c_str());
    ParseScore(text);
    return true;
}
