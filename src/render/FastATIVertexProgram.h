// FastATIVertexProgram (0x100637704..0x1006379d4): four ARB vertex programs (texture-coordinate source: position or
// vertex texcoord, for each of two units) used on "fast ATI" boards. MacOpenGL::IsFastATICard() always answers false in
// the Mac build, so nothing reaches this path, but the code is part of the device's construction and teardown.
#pragma once
#include <cstdint>

namespace sgl {

struct FastATIVertexProgram {
    static std::uint32_t aVertexPrograms[2][2];
    static std::uint32_t pBoundProgram;
    static std::int32_t bEnabled;
    static void Init();       // @ 0x100637704
    static void Shutdown();   // @ 0x1006379d4
};

}  // namespace sgl
