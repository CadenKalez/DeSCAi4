#include "app/NativeAudio.h"

#include "app/GameAudio.h"
#include "gz/WinBtn.h"

namespace native {
namespace {
GameAudio* g_audio = nullptr;

// RZ::SS() / RZ::WCM for the window buttons: the sounds play, the colour manager's fonts and images stay with the window
// code's own fallbacks (HasWindowColorManager answers false).
class ButtonHost final : public gz::IWinButtonHost {
public:
    gz::IWinButtonFont* Font(std::uint32_t) override { return nullptr; }
    cIGZBuffer* Image(std::uint32_t) override { return nullptr; }
    std::uint32_t DefaultSoundId(int) override { return 0; }
    void PlaySound(std::uint32_t soundId) override { PlayGameSound(soundId); }
    void ShowTip(const gz::BtnTipRequest&) override {}
    void HideTip() override {}
    bool HasWindowColorManager() override { return false; }
};
ButtonHost g_buttons;
}  // namespace

void SetGameAudio(GameAudio* audio) { g_audio = audio; }
void PlayGameSound(std::uint32_t soundId) {
    if (g_audio && soundId) g_audio->PlayTrack(soundId);
}
void InstallButtonSounds() { gz::SetWinButtonHost(&g_buttons); }

}  // namespace native
