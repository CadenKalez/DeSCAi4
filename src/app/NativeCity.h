// Loading a city file in the native application: the application's city hooks (the recovered cSC4City, cSC4DBSegment and the
// save-game helpers) and the serializable factory that creates every object of a city file from its class id.
#pragma once
#include <functional>
#include <string>

class cSC4App;

namespace native {

// Installs a neutral stub for every host interface the recovered classes reach (the real hosts replace them as they are written).
void InstallStubHosts();

// The window changes LoadCity asks for once the file is read (the application window's DestroyCurrentScreen / CreateCityScreen).
struct CityScreenCallbacks {
    std::function<void()> destroyCurrentScreen, createCityScreen;
};
void SetCityScreenCallbacks(CityScreenCallbacks callbacks);

// Installs the city hooks once and loads the city file through cSC4App::LoadCity; true when the city came up.
bool LoadCityFile(cSC4App& app, const std::string& path);

}  // namespace native
