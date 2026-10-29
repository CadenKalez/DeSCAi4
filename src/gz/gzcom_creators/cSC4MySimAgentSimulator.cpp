// GZCOM creator for cSC4MySimAgentSimulator. The class needs the host installed with gzcom::SetMySimAgentHost(); without one the creator logs
// SC4_UNWIRED and answers null.
#include "gz/GZCOM.h"
#include "platform/Unwired.h"
#include "sc4/MySimAgentSimulator.h"

namespace gzcom {

namespace {
sc4::IMySimAgentHost* g_mySimAgentHost = nullptr;
}

void SetMySimAgentHost(sc4::IMySimAgentHost* host) { g_mySimAgentHost = host; }

void* GZCOM_CreatecSC4MySimAgentSimulator() {
    if (!g_mySimAgentHost) {
        SC4_UNWIRED("IMySimAgentHost (GZCOM_CreatecSC4MySimAgentSimulator has no host)");
        return nullptr;
    }
    auto* agent = new sc4::cSC4MySimAgentSimulator(g_mySimAgentHost);
    return static_cast<void*>(static_cast<cIGZUnknown*>(static_cast<cIGZSerializable*>(agent)));
}

}  // namespace gzcom
