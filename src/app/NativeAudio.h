// The game's sound ids on the native application: every `PlaySound(id)` of the recovered UI and tools ends here.
#pragma once
#include <cstdint>

class GameAudio;

namespace native {

void SetGameAudio(GameAudio* audio);          // the running sound output (null: silent)
void PlayGameSound(std::uint32_t soundId);    // a Hit track id of Sound.dat
void InstallButtonSounds();                   // the window buttons' sound service

}  // namespace native
