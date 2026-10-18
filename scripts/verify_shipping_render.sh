#!/bin/zsh
# Rendering milestone gate: builds the shipping renderer sources (scripts/shipping_render_sources.txt) into a check binary
# and verifies with otool/nm that it links no OpenGL/CGL/AGL and contains no GL, D3D7/DirectDraw, shim, MetalGL or
# reference-adapter symbols. Also checks build/sc4 (the app) when present. Exit status != 0 on any hit.
cd "$(dirname "$0")/.."
mkdir -p build
SRCS=(${(f)"$(grep -v '^#' scripts/shipping_render_sources.txt | grep .)"})
cat > build/ship_check_main.cpp <<'CPP'
#include <cstdio>
#include "render/gd/MetalGDriver.h"
int main() {
    auto d = gd::cMetalGDriver::Create(nullptr, 16, 16);
    if (!d) { std::puts("no metal device"); return 0; }
    d->ClearColor(1, 0, 0, 1); d->Clear(gd::kClearColor);
    std::vector<std::uint8_t> px; std::uint32_t w, h; d->ReadBack(px, w, h);
    std::printf("ship check: pixel %02x%02x%02x\n", px[2], px[1], px[0]);
}
CPP
clang++ -std=c++20 -O2 -fobjc-arc -Isrc -arch arm64 build/ship_check_main.cpp $SRCS \
  -framework Foundation -framework Metal -framework QuartzCore -o build/render_ship_check 2>&1 | grep -E "error|rror:" && exit 2
BAD='OpenGL|libGL|CGL|AGL|[ _]_?gl[A-Z][A-Za-z0-9]+|DirectDraw|IDirect3D|IDirectDraw|CDirect3D|CDirectDraw|D3DDevice7|MacOpenGL|MetalGL|GDriverOnGL|IGLApi|GLState'
rc=0
check() {
  local bin=$1
  [ -f "$bin" ] || { echo "skip $bin (not built)"; return; }
  if otool -L "$bin" | grep -Ei 'OpenGL|libGL'; then echo "FAIL: $bin links OpenGL"; rc=1; fi
  if nm -a "$bin" 2>/dev/null | grep -E "$BAD"; then echo "FAIL: $bin contains GL / shim / reference symbols"; rc=1; fi
  [ $rc = 0 ] && echo "ok: $bin links no OpenGL and has no GL/shim/reference symbols"
}
check build/render_ship_check
check build/sc4
build/render_ship_check
exit $rc
