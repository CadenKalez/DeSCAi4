#include "sc4/AutomataAttractor.h"

#include <algorithm>

namespace sc4 {

namespace {
IAutomataAttractorHost* g_host = nullptr;
constexpr std::uint16_t kAttractorStreamVersion = 1;
}

IAutomataAttractorHost* AutomataAttractorHost() { return g_host; }
void SetAutomataAttractorHost(IAutomataAttractorHost* host) { g_host = host; }

tControllerTemplate* CreateAttractorTemplate() { return new tAttractorTemplate(); }

bool cSC4AutomataAttractor::Init() {
    if (!initialized_ && template_) {
        strength_ = static_cast<tAttractorTemplate*>(template_)->strengthCurve;
        cSC4AutomataController::Init();
    }
    return true;
}

bool cSC4AutomataAttractor::Write(cIGZOStream& stream) const {
    cSC4AutomataController::Write(stream);   // result ignored
    stream.SetUint16(kAttractorStreamVersion);
    stream << strength_;
    return !stream.GetError();
}

bool cSC4AutomataAttractor::Read(cIGZIStream& stream) {
    cSC4AutomataController::Read(stream);   // result ignored
    std::uint16_t version = 0;
    stream.GetUint16(version);
    stream >> strength_;
    return !stream.GetError();
}

bool cSC4AutomataAttractor::HandleEnterState(IControllerAutomaton* automaton, std::uint32_t state) {
    if (template_ && template_->maxAutomata != 0) {
        if (std::find(automata_.begin(), automata_.end(), automaton) == automata_.end()) {
            automata_.push_back(automaton);
            ++automataCount_;
            automaton->AddRef();
        }
    }
    cSC4AutomataController::HandleEnterState(automaton, state);
    return false;
}

bool cSC4AutomataAttractor::HandleExitState(IControllerAutomaton* automaton, std::uint32_t state) {
    if (!automata_.empty()) {
        auto it = std::find(automata_.begin(), automata_.end(), automaton);
        if (it != automata_.end()) {
            automata_.erase(it);
            --automataCount_;
            automaton->Release();
        }
    }
    cSC4AutomataController::HandleExitState(automaton, state);
    return false;
}

void cSC4AutomataAttractor::SetAutomataGroups(const std::vector<std::uint32_t>& groups) {
    if (!template_ || template_->controllerType != kControllerTypeAttractor) return;
    tControllerTemplate* tmpl = template_;
    if (&tmpl->groups != &groups) tmpl->groups.assign(groups.begin(), groups.end());
}

bool cSC4AutomataAttractor::TestAutomataFilter(cISC4Occupant* occupant) const {
    if (occupant_ == occupant) return false;
    tControllerTemplate* tmpl = template_;
    bool matches = false;
    for (std::uint32_t group : tmpl->groups) {
        if (occupant->IsOccupantGroup(group)) {
            matches = true;
            break;
        }
    }
    if (matches && !tmpl->ignoreStates.empty() && g_host) {
        std::int32_t typeId = 0;
        if (g_host->GetAutomatonStateId(occupant, typeId)) {
            for (std::uint32_t excluded : tmpl->ignoreStates) {
                if (typeId == static_cast<std::int32_t>(excluded)) return false;
            }
        }
    }
    return matches;
}

float cSC4AutomataAttractor::GetCurrentStrength(const ControllerTime& time, float distance) const {
    if (((static_cast<std::uint32_t>(template_->field60) >> (time.days & 0x1f)) & 1) == 0) return 0.0f;
    const float byDistance = strength_.GetOutputValue(distance);
    const float byTime = template_->curve.GetOutputValue(time.time);
    return (1.0f - distance / GetRadius()) * byTime * byDistance;
}

}  // namespace sc4
