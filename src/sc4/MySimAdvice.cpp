#include "sc4/MySimAdvice.h"

namespace sc4 {

static IMySimAdviceHost* g_host = nullptr;
IMySimAdviceHost* MySimAdviceHost() { return g_host; }
void SetMySimAdviceHost(IMySimAdviceHost* host) { g_host = host; }

bool cSC4MySimAdvice::DoLink(const cIGZString& link) {
    if (g_host) g_host->NotifySimOfLink(GetAdvisorID());
    return cSC4Advice::DoLink(link);
}

}  // namespace sc4
