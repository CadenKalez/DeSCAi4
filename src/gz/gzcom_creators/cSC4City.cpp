// GZCOM creator for cSC4City. The city needs the host services installed with gzcom::SetCityHost(); without a host the
// original factory fails and hands back nullptr. The creator is registered here instead of being a second definition of
// GZCOM_CreatecSC4City, so GZCOM.cpp's weak fallback (and GZCOMCreators7.cpp's host-driven definition) can coexist.
#include "gz/GZCOM.h"
#include "sc4/cSC4City.h"

namespace gzcom {

namespace {

void* CreateCity() {
    sc4::ICityHost* host = CityHost();
    return host ? static_cast<void*>(new sc4::cSC4City(host)) : nullptr;
}

// Runs before main (and before the application creates a city) and installs the constructor-backed factory.
struct CityCreatorRegistrar {
    CityCreatorRegistrar() { SetCityCreator(&CreateCity); }
} g_cityCreatorRegistrar;

}  // namespace

}  // namespace gzcom
