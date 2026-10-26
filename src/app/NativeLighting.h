// The native ILightingHost: the game's own cSC4LightingManager running on the recovered terrain, the lighting exemplar and
// the four time-of-day colour maps of the game data.
#pragma once
#include "sc4/DrawContext.h"

namespace sc4 {
class cSTETerrain;
class cSC43DRender;
class cSC4Simulator;
class cSC4LightingManager;
}  // namespace sc4

namespace native {

// Creates the manager for the renderer (IRender3DHost::CreateLightingManager); the renderer owns the returned view.
IS4LightingManager* CreateNativeLightingManager();
// The manager once the city's terrain exists: Init, noon, then light the whole terrain. Safe to call again.
void InitCityLighting(sc4::cSTETerrain* terrain, sc4::cSC43DRender* render, sc4::cSC4Simulator* simulator);
// Advances the time of day from the simulation clock; call once per frame.
void TickLighting();
sc4::cSC4LightingManager* LightingManager();

}  // namespace native
