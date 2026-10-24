// Interface ids several sc4 headers need. They used to be defined again in each header that used them, which made those
// headers impossible to include together in one translation unit (a redefinition error) - and the engine wiring that
// installs the classes' hosts has to include many of them at once. One definition, here.
#pragma once
#include <cstdint>

namespace sc4 {

constexpr std::uint32_t kGZIID_cIGZMessageTarget2 = 0x452294aa;
constexpr std::uint32_t kGZIID_cIGZSerializable = 0xe4fda3d4;

}  // namespace sc4
