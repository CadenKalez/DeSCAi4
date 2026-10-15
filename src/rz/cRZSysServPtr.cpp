#include "rz/cRZSysServPtr.h"

namespace {
cRZServiceResolver g_resolver = nullptr;
}
void SetRZServiceResolver(cRZServiceResolver resolver) { g_resolver = resolver; }
cRZServiceResolver GetRZServiceResolver() { return g_resolver; }
