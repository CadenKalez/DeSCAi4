#include "audio/platform/AudioPlatform.h"

namespace AudioPlatform {
namespace {
IBackend* g_backend = nullptr;
}
IBackend* GetBackend() { return g_backend; }
void SetBackend(IBackend* backend) { g_backend = backend; }
}  // namespace AudioPlatform
