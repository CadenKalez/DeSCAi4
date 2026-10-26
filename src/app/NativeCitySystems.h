// The service locator's city-phase table (SL::spTerrain, SL::spLotManager, the simulators ...) filled from the loaded city's
// components: what the original does in OnCityCreate / OnCityInit.
#pragma once

class cISC4City;
namespace native {
void BindCityServices(cISC4City* city);
}
