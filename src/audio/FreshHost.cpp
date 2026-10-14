#include "audio/FreshHost.h"

namespace Fresh {
namespace {
cIFreshHost* g_host = nullptr;
}
cIFreshHost* GetHost() { return g_host; }
void SetHost(cIFreshHost* host) { g_host = host; }
}  // namespace Fresh
