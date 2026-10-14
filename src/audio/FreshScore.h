// cFreshScore — a "Freshness" music score: a header of tuning values plus a rows x columns grid of
// cFreshCell. Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100ab74c8).
//
// Original layout (0x8050): +0x08 value (0), header ints at +0x0c..+0x48, 64x64 pointer grid at +0x50.
// Header names below are justified by cFreshPlayer's use of each field (see comments).
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "audio/FreshCell.h"
#include "audio/interfaces/cIFreshScore.h"

class cFreshScore final : public cIFreshScore {
public:
    static constexpr int kMaxRows = 64;
    static constexpr int kMaxColumns = 64;

    struct Header {
        std::int32_t unknown08 = 0;                  // +0x08 never read by the player
        std::int32_t volumeScale = 0x400;            // +0x0c  scales every cell's volume (1024 = unity)
        std::int32_t discreteMode = 0;               // +0x10  nonzero: positions snap to the selection area
                                                     //         and cells are removed by GetCellToSubtract
        std::int32_t minCellsPercentWeight = 0;      // +0x14  play% * this / 100 adds to the minimum cells
        std::int32_t maxCellsAtFullPercent = 4;      // +0x18  max cells = play% * this / 100
        std::int32_t minCellsColumnWeight = 0;       // +0x1c  (x+1) * this / (columns-1) adds to the minimum
        std::int32_t minCellsRowWeight = 0;          // +0x20  (y+1) * this / (rows-1) adds to the minimum
        std::int32_t rowRadius = 2;                  // +0x24  selection area half-height
        std::int32_t columnRadius = 2;               // +0x28  selection area half-width
        std::int32_t rows = 4;                       // +0x2c
        std::int32_t columns = 4;                    // +0x30
        std::int32_t tempo = 120;                    // +0x34  beats per minute at Play()
        std::int32_t beatsPerCycle = 8;              // +0x38  beat counter wraps here; cells change on
                                                     //         beats (beatsPerCycle-1) and (beatsPerCycle-6)
        std::int32_t headerValue3C = 1;              // +0x3c  parsed; not read by cFreshPlayer
        std::int32_t headerValue40 = 1;              // +0x40  parsed; not read by cFreshPlayer
        std::int32_t initialXPosition = 0;           // +0x44  not in the file; passed to SetXPos on Play()
        std::int32_t initialYPosition = 0;           // +0x48  not in the file; passed to SetYPos on Play()
    };

    cFreshScore() = default;
    virtual ~cFreshScore();

    // cFreshScore::Create() @ 0x10043abee
    static cFreshScore* Create() { return new cFreshScore(); }

    bool QueryInterface(std::uint32_t iid, void** out) override;  // only cIGZUnknown(1)
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    cIFreshCell* GetCell(std::uint32_t row, std::uint32_t column, std::uint32_t) override {
        return row < kMaxRows && column < kMaxColumns ? grid_[row][column] : nullptr;
    }

    // Parses score text (versions 5, 6 and 7). Always returns true like the original, even when
    // the text is not a recognised score. Takes ownership semantics of the cells it creates.
    bool ParseScore(cRZString& text);
    // Loads a whole file as text and parses it. False if it cannot be read or is empty.
    bool LoadScoreFromPath(const std::string& path);

    Header header;

private:
    void ClearGrid();

    std::int32_t refCount_ = 0;
    std::array<std::array<cFreshCell*, kMaxColumns>, kMaxRows> grid_{};
};
