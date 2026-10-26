#include "app/NativeAudio.h"

#include "app/GameAudio.h"
#include <cstdio>
#include <cstdlib>

#include "audio/AudioServices.h"
#include "audio/interfaces/cIGZSnd.h"
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
    if (!soundId) return;
    // The game's own path first: the sound manager builds the sound from its track definition and the hit manager plays it.
    if (cIGZSndMgr* manager = Audio::SoundManagerService()) {
        cIGZSnd* sound = nullptr;
        if (manager->GetSoundById(soundId, 0, nullptr, kGZIID_cIGZSnd, reinterpret_cast<void**>(&sound)) && sound) {
            const bool played = sound->PlayWithVolumeAndLoop(0x400, false, 0);
            if (std::getenv("SC4_DEBUG")) std::fprintf(stderr, "audio: sound %08x played %d\n", soundId, int(played));
            sound->Release();
            if (played) return;
        }
    }
    if (g_audio) g_audio->PlayTrack(soundId);   // the direct track lookup when the sound system is not up
}
void InstallButtonSounds() { gz::SetWinButtonHost(&g_buttons); }

}  // namespace native
