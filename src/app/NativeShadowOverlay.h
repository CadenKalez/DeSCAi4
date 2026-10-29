// Occupant shadows on the native Metal application: the recovered cSTEOverlayManager keeps the shadow decals the model maker adds
// (AddShadow projects the model along the light with the recovered lighting manager); a view object after the terrain draws them as
// terrain-conforming triangles whose texture coordinates are the overlay's shadow matrix applied to the world position.
#pragma once

#include <cstdint>

namespace sc4 { class cSC43DRender; class cSTETerrain; class IShadowOverlay; }

namespace native {

// Creates the overlay manager for this terrain and puts its drawing in the render (once per city view).
void AttachShadowOverlay(sc4::cSC43DRender& render, sc4::cSTETerrain& terrain);
// The model maker's view of it (null before AttachShadowOverlay).
sc4::IShadowOverlay* TheShadowOverlay();
// What an occupant's placeable object does with the overlay ids it was given (models rebuilt / occupant hidden).
void RemoveShadowOverlay(std::uint32_t id);
void SetShadowOverlayEnabled(std::uint32_t id, bool enabled);

}  // namespace native
