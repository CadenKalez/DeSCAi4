#include "gz/PersistStreams.h"

namespace {
IPersistHost* g_host = nullptr;
}

IPersistHost* PersistHost() { return g_host; }
void SetPersistHost(IPersistHost* host) { g_host = host; }
