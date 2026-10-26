// The audio directors' start-up on the native application (see NativeAudioDirector.cpp).
#pragma once
#include <string>

class cGZFramework;

namespace native {

// Creates the sound system, sound manager, hit manager and the game's sound configuration. `dataDirectory` is GameData/.
bool StartAudioDirectors(cGZFramework& framework, const std::string& dataDirectory);

}  // namespace native
