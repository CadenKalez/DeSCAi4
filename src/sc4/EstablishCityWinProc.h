// cSC4EstablishCityWinProc — the "establish city" dialog (15 functions, 0x100376430..0x100376d10): city and mayor
// name edit boxes (0xaa4149bf / 0xaa4149d2), three difficulty buttons 0x2ab6c373..0x2ab6c375 (one pressed at a time,
// +0x2c holds the choice) and an OK button 0x2a4144be that is enabled only while both names have a visible character.
// Enter confirms (when valid), Tab moves between the two edit boxes (selecting the text of the one entered), losing
// focus puts it back on the city name. Closing ends the dialog with result "not ok" (or detaches a non-modal window).
#pragma once
#include <cstdint>

#include "gz/WinGen.h"
#include "gz/WinProc.h"
#include "gz/WinTextEdit.h"

namespace sc4 {

class IEstablishCityHost {
public:
    virtual ~IEstablishCityHost() = default;
    virtual int DifficultyLevel() = 0;                         // spCity->GetDifficultyLevel()
    virtual cIGZWin* FocusedWindow() = 0;                      // window manager GZGetFocus
    virtual void SetFocus(cIGZWin* window) = 0;                // window manager GZSetFocus
};
void SetEstablishCityHost(IEstablishCityHost* host);

class cSC4EstablishCityWinProc final : public gz::cIGZWinProc {
public:
    explicit cSC4EstablishCityWinProc(gz::cIGZWinGen* dialog);
    ~cSC4EstablishCityWinProc();
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) override;

    void InitDialog();
    bool ValidateCityAndMayorNames();
    bool GZOnCharacter(char c);
    bool DoCommand(const cGZMessage& message);
    void EndDialog(bool ok);
    bool Shutdown() { EndDialog(false); return true; }
    int Difficulty() const { return difficulty_; }

private:
    std::uint32_t refCount_ = 0;        // +8
    int difficulty_ = 0;                // +0x2c
    gz::cIGZWinGen* dialog_;            // +0x30
};

}  // namespace sc4
