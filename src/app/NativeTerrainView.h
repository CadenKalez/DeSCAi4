// The terrain in the city view on the native Metal application: the recovered terrain view (cSTETerrainView3D) drawn through the
// recovered draw context (SC4DrawContext) onto the Metal driver, its textures decoded from the game's FSH resources.
#pragma once

namespace gd { class cIGZGDriver; }
namespace sc4 { class cSC43DRender; class cSTETerrain; }

namespace native {

// Creates the terrain view for `terrain` and registers its terrain and water passes with the renderer.
bool AttachTerrainView(sc4::cSC43DRender& render, sc4::cSTETerrain& terrain, gd::cIGZGDriver* driver);

}  // namespace native
