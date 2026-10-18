// The DX7 shim's production host: builds the real surface / vertex-buffer classes and answers the display queries from
// MacDisplay. Not a recovered function; it is the glue the original gets from direct calls.
#pragma once
#include "render/DirectDraw7.h"

namespace sgl {
IDX7Host* DefaultDX7Host();
}
