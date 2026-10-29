// cSC4PatrolCar — see sc4/PatrolCar.h.
#include "sc4/PatrolCar.h"

#include "sc4/DBSegment.h"

namespace sc4 {

cSC4PatrolCar::cSC4PatrolCar(ITrafficAutomatonHost* host, IAutomatonHost* automatonHost, IVehicleHost* vehicleHost,
                             IEmergencyVehicleHost* emergencyHost, IPatrolCarHost* patrolHost, const cGZPersistResourceKey& modelKey)
    : cSC4EmergencyVehicle(host, automatonHost, vehicleHost, emergencyHost, modelKey), patrolHost_(patrolHost) {}

cSC4PatrolCar::~cSC4PatrolCar() {
    if (riot_) { cIGZUnknown* old = riot_; riot_ = nullptr; old->Release(); }
}

// 0x10050ea04: the crew goes first, then the emergency vehicle shuts down.
bool cSC4PatrolCar::Shutdown() {
    ReleaseRiotPolice();
    return cSC4EmergencyVehicle::Shutdown();
}

// 0x10050ea20: the city's riot-police manager forgets the crew, then the car lets go of it.
void cSC4PatrolCar::ReleaseRiotPolice() {
    if (!riot_) return;
    if (patrolHost_) patrolHost_->RemoveRiotPolice(riot_);
    if (riot_) {
        cIGZUnknown* old = riot_;
        riot_ = nullptr;
        old->Release();
    }
}

// 0x10050eab0: the reach of the station that sent the car, or 16 cells when the station is unknown.
float cSC4PatrolCar::GetEffectiveRadius() const {
    float radius = 0.0f;
    if (dispatchSource_ != nullptr && patrolHost_ && patrolHost_->StationRadius(dispatchSource_, radius)) return radius;
    return kPatrolCarDefaultRadius;
}

// 0x10050eb1e: the crew is done, the call is over.
void cSC4PatrolCar::HandleCrewFinished() {
    ReleaseRiotPolice();
    SetDispatchState(2);
}

// 0x10050ec86: one crew per car, competence from the station that sent it (1.0 when unknown).
void cSC4PatrolCar::CreateRiotPolice() {
    if (riot_ != nullptr || !patrolHost_) return;
    cIGZUnknown* crew = patrolHost_->NewRiotPolice(*this);
    cIGZUnknown* old = riot_;
    if (old != crew) {
        if (crew) crew->AddRef();
        riot_ = crew;
        if (old) old->Release();
    }
    float competence = 1.0f;
    if (dispatchSource_ != nullptr) {
        float value = 0.0f;
        if (patrolHost_->PoliceCompetence(dispatchSource_, value)) competence = value;
    }
    patrolHost_->SetupRiotPolice(riot_, competence);
}

// 0x10050eb46. During a riot an arrival (5) or a blockade (1) brings the crew; without one the car pushes the arrest state when a
// criminal is within 20 cells, the idle state otherwise. A player-driven car (state 0x10) just drops the crew.
void cSC4PatrolCar::SetDispatchState(std::uint32_t state) {
    cSC4EmergencyVehicle::SetDispatchState(state);
    if (FindState(kStatePlayerDrive) != nullptr) { ReleaseRiotPolice(); return; }
    const bool riot = patrolHost_ && patrolHost_->RiotInProgress();
    const std::uint32_t now = GetDispatchState();
    if (now == 1) {
        ReleaseRiotPolice();
        CreateRiotPolice();
        return;
    }
    if (now != 5) { ReleaseRiotPolice(); return; }
    ReleaseRiotPolice();
    if (riot) { CreateRiotPolice(); return; }
    const cS3DVector3 here = position_;
    const bool anyCrime = patrolHost_ &&
        (patrolHost_->FindCrimeAutomaton(here, kCrimeSearchRadius) != nullptr || patrolHost_->FindCrimeParticipant(here, kCrimeSearchRadius) != nullptr);
    PushState(anyCrime ? kPatrolStateArrest : kStateIdleTick, nullptr, kPatrolIdleStateSeconds, false, 0, 0, nullptr);
}

// 0x10050edc8: with no riot a finished call sends the car home and a call it is standing at is finished.
bool cSC4PatrolCar::DoHandlingEventTick(float milliseconds) {
    (void)milliseconds;
    const bool riot = patrolHost_ && patrolHost_->RiotInProgress();
    const std::uint32_t now = GetDispatchState();
    if (now == 2) {
        if (!riot) ReturnToSourceOccupant();
    } else if (now == 5 && !riot) {
        SetDispatchState(2);
    }
    return true;
}

// 0x10050ee38: version 2, the emergency vehicle's record, the crew.
bool cSC4PatrolCar::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    const bool ok = cSC4EmergencyVehicle::Write(stream);
    if (ok) SC4WriteGZSerializable(stream, riot_);
    return ok;
}

// 0x10050eeb0.
bool cSC4PatrolCar::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 2 || !cSC4EmergencyVehicle::Read(stream)) return false;
    void* raw = nullptr;
    SC4ReadGZSerializable(stream, kAutomatonIid, &raw, false);
    cIGZUnknown* crew = static_cast<cIGZUnknown*>(raw);
    cIGZUnknown* old = riot_;
    if (old != crew) {
        if (crew) crew->AddRef();
        riot_ = crew;
        if (old) old->Release();
    }
    return true;
}

// 0x10050ef76: a player takes the wheel (state 0x10) and the crew goes home; the base decides the rest, and any state at all counts as handled.
bool cSC4PatrolCar::HandleEnterState(tAutomataState* state) {
    if (state != nullptr && state->stateType == kStatePlayerDrive) ReleaseRiotPolice();
    const bool handled = cSC4EmergencyVehicle::HandleEnterState(state);
    return handled || state != nullptr;
}

// 0x10050efba: the arrest state has its own tick, everything else is the emergency vehicle's.
bool cSC4PatrolCar::DoStateTick(float milliseconds) {
    tAutomataState* state = GetState();
    if (state == nullptr) return false;
    if (state->stateType != kPatrolStateArrest) return cSC4EmergencyVehicle::DoStateTick(milliseconds);
    DoArrestTick(milliseconds);
    return true;
}

// 0x10050f28a: leaving the arrest state puts the car back on its first animation.
bool cSC4PatrolCar::HandleExitState(const tAutomataState* state) {
    if (cSC4EmergencyVehicle::HandleExitState(state)) return true;
    if (state == nullptr) return false;
    if (state->stateType == kPatrolStateArrest) PlayAnim(0u);
    return true;
}

// 0x10050f00e. While the arrest timer runs it counts down; when it reaches zero everybody within 20 cells is arrested (up to five
// participants), or the lone criminal automaton fades out and leaves; the arrest animation of the station's competence replaces the
// car's model and plays once. After the timer the state ends with the last frame of the animation.
bool cSC4PatrolCar::DoArrestTick(float milliseconds) {
    tAutomataState* state = states_.back();
    float timer = state->timer;
    if (timer > 0.0f) {                                             // (jbe: zero, negative and NaN take the animation branch)
        timer -= milliseconds;
        state->timer = timer;
        if (0.0f < timer || timer != timer) return true;            // (jc: still counting, or NaN)
        if (patrolHost_) {
            const cS3DVector3 here = position_;
            cSC4Automaton* criminal = patrolHost_->FindCrimeAutomaton(here, kCrimeSearchRadius);
            ICrimeParticipant* participant = patrolHost_->FindCrimeParticipant(here, kCrimeSearchRadius);
            if (participant == nullptr) {
                if (criminal != nullptr) {
                    criminal->SetOpacity(kCriminalFadeOpacity, kCriminalFadeSeconds);
                    criminal->PushState(kStateIdleTick, nullptr, kCriminalLeaveStateSeconds, true, 0, 0, nullptr);
                }
            } else {
                std::uint32_t count = 1;
                do {
                    participant->Arrest(true);
                    participant = patrolHost_->FindCrimeParticipant(here, kCrimeSearchRadius);
                    if (count > 4) break;
                    ++count;
                } while (participant != nullptr);
            }
        }
        ShowEmergencyLights(false);
        cGZPersistResourceKey key = primaryKey_;
        if (key.instance != 0 && models_.size() > 0 && patrolHost_) {
            std::int32_t index = 0;
            float competence = 0.0f;
            if (dispatchSource_ != nullptr && patrolHost_->PoliceCompetence(dispatchSource_, competence))
                index = static_cast<std::int32_t>((competence * 100.0f) / 40.0f);
            if (index < 0) index = 0;                               // (the original indexes past its 4-entry table for a competence above 1.6)
            if (index > 3) index = 3;
            key.instance = kArrestAnimationInstance[index];
            patrolHost_->ReplaceArrestModel(*this, key);
        }
        return true;
    }
    IAutomatonModel* model = models_.front();
    if (model->GetFrame() >= model->GetFrameCount() - 1) PopState();
    return true;
}

}  // namespace sc4
