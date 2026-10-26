// Zone colours over the terrain in the city view on the native Metal application. The original paints these through the
// overlay manager (cSTEOverlayManager decals, not wired natively yet): this draws one translucent quad per cell on the
// terrain, in the zone's drag colour - the zone grid's cells (the zones already placed) and the cells of the drag in
// progress - through the recovered draw context as a view object after the terrain.
#pragma once
#include <cstdint>

namespace sc4 { class cSC43DRender; class cSTETerrain; class cSC4ZoneManager; }

namespace native {

void AttachZoneOverlay(sc4::cSC43DRender& render, sc4::cSTETerrain& terrain);
void SetZoneOverlayManager(sc4::cSC4ZoneManager* zones);          // the zone grid to show (null: none)
void SetDragHighlightCell(int x, int z, std::uint32_t argb);      // one cell of the drag preview
void ClearDragHighlight();
void SetDragErrorRect(int x0, int z0, int x1, int z1);            // the failed selection, drawn red
void ClearDragError();

}  // namespace native
