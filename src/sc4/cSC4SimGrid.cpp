#include "sc4/cSC4SimGrid.h"

namespace sc4 {

static ISimGridCity* g_city = nullptr;
ISimGridCity* SimGridCity() { return g_city; }
void SetSimGridCity(ISimGridCity* city) { g_city = city; }

// Instantiate the ten grids the game creates.
template class cSC4SimGrid<std::uint8_t>;
template class cSC4SimGrid<std::int8_t>;
template class cSC4SimGrid<std::uint16_t>;
template class cSC4SimGrid<std::int16_t>;
template class cSC4SimGrid<std::uint32_t>;
template class cSC4SimGrid<std::int32_t>;
template class cSC4SimGrid<std::uint64_t>;
template class cSC4SimGrid<std::int64_t>;
template class cSC4SimGrid<float>;
template class cSC4SimGrid<double>;

}  // namespace sc4
