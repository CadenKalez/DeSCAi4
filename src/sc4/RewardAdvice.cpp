#include "sc4/RewardAdvice.h"

namespace sc4 {

static IRewardAdviceHost* g_host = nullptr;
IRewardAdviceHost* RewardAdviceHost() { return g_host; }
void SetRewardAdviceHost(IRewardAdviceHost* host) { g_host = host; }

bool cSC4RewardAdvice::Init(int adviceId) {
    const bool ok = cSC4Advice::Init(adviceId);
    SetFlags(kAdviceFlagReward, true);
    return ok;
}

bool cSC4RewardAdvice::GetTrigger() const {
    IAdviceHost* advisor = AdviceHost();
    IRewardAdviceHost* sims = g_host;
    auto* self = const_cast<cSC4RewardAdvice*>(this);
    advisor->BeginScript(self);
    IScriptLua* lua = advisor->Lua();

    std::uint32_t state = 1;
    cRZString reason;
    const std::uint32_t guid = static_cast<std::uint32_t>(guid_);
    sims->GetConditionState(guid, state, reason);   // keeps the defaults when there is no record

    const int top = lua->GetTop();
    lua->GetGlobal("advices");
    lua->ObjLen(-1);   // length is fetched and dropped
    lua->PushNumber(static_cast<double>(adviceId_));
    lua->RawGet(-2);
    if (!lua->IsTable(-1)) {
        lua->SetTop(top);
        advisor->EndScript();
        return false;
    }
    lua->PushString("condition");
    lua->RawGet(-2);
    bool fired = false;
    if (lua->CallProtected(0, 1) == 0) {
        if (lua->Type(-1) == kScriptLuaTypeString) {
            const char* script = lua->ToString(-1);
            cADVString text(script);
            if (text.Load(true, false)) {
                text.ConvertTokens(nullptr, true);
                reason.Copy(text);
            } else {
                reason.AssignFromCString(script);
            }
            state &= ~3u;
        } else if (lua->IsNumber(-1)) {
            const int result = static_cast<int>(lua->ToNumber(-1));
            if (result == 1) {
                if ((state & kRewardStateFired) == 0) {
                    std::uint32_t word0 = 0, word1 = 0;
                    sims->GetBuildingWords(guid, word0, word1);
                    cISC4AdviceSubject* subject = sims->CreateSubject();
                    subject->SetData1(word0);
                    subject->SetData2(word1);
                    self->SetEventSubject(subject);
                    subject->Release();
                    fired = true;
                }
                state = (state & 0xffffff00u) | 6u;
            } else if (result == 0) {
                state = (state & ~3u) | 1u;
            }
        }
    }
    // else: the original formats "cSC4RewardAdvice::GetTrigger() -- error while executing a trigger script ..." and drops it
    sims->SetConditionState(guid, state, reason);
    lua->SetTop(top);
    advisor->EndScript();
    return fired && !CheckFlags(kAdviceFlagNoText);
}

}  // namespace sc4
