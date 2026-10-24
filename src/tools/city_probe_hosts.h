// city_probe_hosts - the getters city_probe_creators.cpp uses to reach the stub hosts installed by
// city_probe_bindings.cpp. The concrete Probe* classes stay private to that translation unit, so the
// creators only see the interface types through these functions.
#pragma once

namespace sc4 {
class IDemolitionHost;
class ICrimeHost;
class IWealthHost;
class INeighborsHost;
class IWeatherHost;
class ICommercialHost;
class ILandValueHost;
class IDemandHost;
class IClockHost;

IDemolitionHost* CityProbeDemolitionHost();
ICrimeHost* CityProbeCrimeHost();
IWealthHost* CityProbeWealthHost();
INeighborsHost* CityProbeNeighborsHost();
IWeatherHost* CityProbeWeatherHost();
ICommercialHost* CityProbeCommercialHost();
ILandValueHost* CityProbeLandValueHost();
IDemandHost* CityProbeDemandHost();
IClockHost* CityProbeClockHost();
}  // namespace sc4
