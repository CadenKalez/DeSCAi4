// Loading a city file in the native application: the application's city hooks (the recovered cSC4City, cSC4DBSegment and the
// save-game helpers) and the serializable factory that creates every object of a city file from its class id.
#pragma once
#include <string>

class cSC4App;

namespace native {

// Installs the city hooks once and loads the city file through cSC4App::LoadCity; true when the city came up.
bool LoadCityFile(cSC4App& app, const std::string& path);

}  // namespace native
