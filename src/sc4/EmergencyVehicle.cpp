// cSC4EmergencyVehicle — see sc4/EmergencyVehicle.h for what is and is not recovered.
#include "sc4/EmergencyVehicle.h"

#include "sc4/DBSegment.h"

namespace sc4 {

cSC4EmergencyVehicle::cSC4EmergencyVehicle(ITrafficAutomatonHost* host, IAutomatonHost* automatonHost,
                                           IVehicleHost* vehicleHost, IEmergencyVehicleHost* emergencyHost,
                                           const cGZPersistResourceKey& modelKey)
    : cSC4Vehicle(host, automatonHost, vehicleHost, modelKey), emergencyHost_(emergencyHost) {}

cSC4EmergencyVehicle::~cSC4EmergencyVehicle() {
    if (dispatchSource_) { cIGZUnknown* old = dispatchSource_; dispatchSource_ = nullptr; old->Release(); }
    if (lightModel_) { IAutomatonModel* old = lightModel_; lightModel_ = nullptr; old->Release(); }
    if (lightEffect_) { cIGZUnknown* old = lightEffect_; lightEffect_ = nullptr; old->Release(); }
}

void cSC4EmergencyVehicle::SetDispatchSource(cIGZUnknown* source) {
    if (dispatchSource_ == source) return;
    if (source) source->AddRef();
    cIGZUnknown* old = dispatchSource_;
    dispatchSource_ = source;
    if (old) old->Release();
}

// 0x1002f56dc: the dispatch source and the light model are released, the siren is silenced, then the vehicle shuts down.
bool cSC4EmergencyVehicle::Shutdown() {
    if (dispatchSource_) {
        cIGZUnknown* old = dispatchSource_;
        dispatchSource_ = nullptr;
        old->Release();
    }
    if (lightModel_) {
        IAutomatonModel* old = lightModel_;
        lightModel_ = nullptr;
        old->Release();
    }
    if (emergencyHost_) emergencyHost_->SirenStopped(*this);
    return cSC4Vehicle::Shutdown();
}

// 0x1002f7f02: version 2, the vehicle record, the destination, the dispatch source, repath counter, call number and state.
bool cSC4EmergencyVehicle::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    const bool ok = cSC4Vehicle::Write(stream);
    if (ok) {
        stream << dispatchDestination_;
        SC4WriteGZSerializable(stream, dispatchSource_);
        stream.SetUint8(repathCount_);
        stream.SetUint16(callNumber_);
        stream.SetUint32(dispatchState_);
    }
    return ok;
}

bool cSC4EmergencyVehicle::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 2) return false;
    if (!cSC4Vehicle::Read(stream)) return false;
    stream >> dispatchDestination_;
    void* raw = nullptr;
    SC4ReadGZSerializable(stream, kEmergencyDispatchedObjectIid, &raw, false);
    if (dispatchSource_) dispatchSource_->Release();            // (the reader stores straight into the slot)
    dispatchSource_ = static_cast<cIGZUnknown*>(raw);
    stream.GetUint8(repathCount_);
    stream.GetUint16(callNumber_);
    stream.GetUint32(dispatchState_);
    return true;
}

// 0x1002f576e: after the vehicle tick, an automaton in emergency mode that is not being removed clears the cars around its
// network occupant and keeps the siren positioned on it.
bool cSC4EmergencyVehicle::DoAnimationTick(std::uint32_t unknown, float milliseconds) {
    const bool result = cSC4Vehicle::DoAnimationTick(unknown, milliseconds);
    if (CheckAutomataFlag(0x80) && !GetMarkedForRemove()) {
        RemoveSurroundingAutomata(networkOccupant_, 0);
        if (emergencyHost_) {
            float position[3];
            GetPosition(position);
            emergencyHost_->SirenMoved(*this, static_cast<int>(position[0]), static_cast<int>(position[1]),
                                       static_cast<int>(position[2]));
        }
    }
    return result;
}

// 0x1002f78e6.
bool cSC4EmergencyVehicle::ShouldCollide(cSC4Automaton* other) const {
    if (!CheckAutomataFlag(0x80)) return cSC4Vehicle::ShouldCollide(other);
    if (this == other) return false;
    if (!other) return false;
    if (other->CheckAutomataFlag(0x80)) {
        if (!other->CheckAutomataFlag(0x20)) return true;
        if (route_.empty()) return lookahead_.size() < 3;
    }
    return false;
}

// 0x1002f57fc: the four emergency states have their own handlers; every other state runs the vehicle tick, and the tick
// reports "done" when it ends the state it started in.
bool cSC4EmergencyVehicle::DoStateTick(float milliseconds) {
    if (states_.empty()) return false;
    tAutomataState* state = GetState();
    if (!state) return false;
    switch (state->stateId) {
        case kEmergencyStateEnRoute: return DoEnRouteTick(milliseconds);
        case kEmergencyStateRepathing: return DoRepathingTick(milliseconds);
        case kEmergencyStateHandlingEvent: return DoHandlingEventTick(milliseconds);
        case kEmergencyStateTeleport: return DoTeleportTick(milliseconds);
        default:
            if (cSC4Vehicle::DoStateTick(milliseconds)) return true;
            return state != GetState();
    }
}

// 0x1002f58d4: drive (along the path, or straight when paths are ignored), pick the next destination on arrival, and keep the
// leader reference fresh while queueing.
bool cSC4EmergencyVehicle::DoEnRouteTick(float milliseconds) {
    if (!GetReachedDestination() && networkOccupant_ != nullptr) {
        if (!CheckAutomataFlag(0x2000)) cSC4Vehicle::DoPathTick(milliseconds);
        else DoStraightLineTick(milliseconds);
    }
    if (GetReachedDestination() && !GetMarkedForRemove() && !ChooseNewDestination()) {
        if (!GetSpecialRouteFlag()) Remove(false);
        else HandleReachedRouteDestination();
    }
    if (!ValidateNetworkOccupant()) {
        Remove(false);
    } else if (!route_.empty()) {
        if (CheckAutomataFlag(0x20) && leader_ != nullptr && leader_->CheckAutomataFlag(0x20)) {
            cSC4Automaton* leader = FindLeader();
            if (leader_ != leader) {
                if (leader) leader->AddRef();
                cSC4Automaton* old = leader_;
                leader_ = leader;
                if (old) old->Release();
            }
        }
    }
    return true;
}

bool cSC4EmergencyVehicle::DoPlayerDriveTick(float milliseconds) { return cSC4Vehicle::DoPlayerDriveTick(milliseconds); }

// 0x1002f5fa8: emergency automata (and ones in the pursuit state) do not attract anything; their attraction is dropped.
void cSC4EmergencyVehicle::UpdateAttraction() {
    if (!CheckAutomataFlag(0x80) && GetBehavior() != kStatePursuit) {
        cSC4Vehicle::UpdateAttraction();
        return;
    }
    if (attractionController_) {
        cSC4AutomataController* old = attractionController_;
        attractionController_ = nullptr;
        old->Release();
    }
}

// 0x1002f600c: the lights key (9) toggles the lights when it is released.
bool cSC4EmergencyVehicle::HandlePlayerDriveCommand(std::uint32_t command, bool on) {
    if (command == kEmergencyPlayerDriveLightsCommand && !on) {
        ShowEmergencyLights(lightModel_ == nullptr);
        return true;
    }
    return cSC4Vehicle::HandlePlayerDriveCommand(command, on);
}

// 0x1002f603c.
bool cSC4EmergencyVehicle::HandleEnterState(tAutomataState* state) {
    if (state) {
        const std::uint32_t id = state->stateId;
        if (id == kStatePlayerDrive) {
            cSC4Vehicle::HandleEnterState(state);
            dispatchState_ = 3;
            SendCallbackMessage(7, GetDispatchState());
            ShowEmergencyLights(false);
            if (emergencyHost_) emergencyHost_->SirenStopped(*this);
            return true;
        }
        if (id == kStatePursuit) ShowEmergencyLights(true);
        else if (id == kEmergencyStateTeleport) cSC4Automaton::SetOpacity(0.0f, cSC4TrafficAutomaton::Tuning().fadeDuration);
    }
    return cSC4Vehicle::HandleEnterState(state);
}

// 0x1002f611e: leaving the player-drive or pursuit state turns the lights off.
bool cSC4EmergencyVehicle::HandleExitState(const tAutomataState* state) {
    if (state && (state->stateId == kStatePlayerDrive || state->stateId == kStatePursuit)) ShowEmergencyLights(false);
    return cSC4TrafficAutomaton::HandleExitState(state);
}

// 0x1002f62e6: an emergency vehicle that loses its road counts as having reached its destination.
void cSC4EmergencyVehicle::HandleNetworkOccupantRemoved() {
    if (CheckAutomataFlag(0x80)) {
        HandleReachedRouteDestination();
        return;
    }
    cSC4TrafficAutomaton::HandleNetworkOccupantRemoved();
}

// The three below are not recovered (their argument registers are lost in the decompile); they are declared so the recovered
// handlers link, and behave as the most conservative reading: the occupant check only reports whether one is set, and the two
// ticks keep the state alive without acting.
bool cSC4EmergencyVehicle::ValidateNetworkOccupant() { return networkOccupant_ != nullptr; }
bool cSC4EmergencyVehicle::DoRepathingTick(float) { return true; }
bool cSC4EmergencyVehicle::DoTeleportTick(float) { return true; }

void cSC4EmergencyVehicle::ShowEmergencyLights(bool on) {
    if ((lightModel_ != nullptr) != on && emergencyHost_) emergencyHost_->SetEmergencyLights(*this, on);
}

}  // namespace sc4
