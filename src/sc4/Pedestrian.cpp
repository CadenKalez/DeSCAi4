// cSC4Pedestrian — see Pedestrian.h for the class, the layout, the vtable slots and the quirks. Every function here is a
// hand port of the exported decompile; the crowd/queue/strike/riot/ignite ticks that still depend on an unrecovered
// neighbour are listed under PARTIAL in notes/ledger_pending/cPedestrian-vehicle.txt.
#include "sc4/Pedestrian.h"

#include <cmath>

#include "sc4/PathInfo.h"

namespace sc4 {

namespace {

cS3DVector3 MakeVector(float x, float y, float z) {
    cS3DVector3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}
cS3DVector3 Scale(const cS3DVector3& v, float s) { return MakeVector(v.x * s, v.y * s, v.z * s); }
float Length(const cS3DVector3& v) { return std::sqrt(v.z * v.z + v.y * v.y + v.x * v.x); }

}  // namespace

const PedestrianTuning& cSC4Pedestrian::Tuning() const {
    static const PedestrianTuning kDefaults{};
    const PedestrianTuning* tuning = pedestrianHost_ ? pedestrianHost_->PedestrianTuningTable() : nullptr;
    return tuning ? *tuning : kDefaults;
}

// ---- construction / destruction ----------------------------------------------------------------------------------------
// 0x1000f1610: the traffic automaton constructor, then the pedestrian fields: the occupant type id 0x496e5783, the crowd
// reaction state/direction cleared, the animation speed 0.01, and the default speed is a random value in the manager's
// range * 1/3600 (the speed tween is then told to reach it with a zero duration).
cSC4Pedestrian::cSC4Pedestrian(IPedestrianHost* host, const cGZPersistResourceKey& modelKey)
    : cSC4TrafficAutomaton(host, AutomatonHost(), modelKey), pedestrianHost_(host ? host : nullptr) {
    type_ = kPedestrianOccupantType;
    crowdReactionState_ = 0;
    crowdReactionDirectionSet_ = false;
    crowdReactionDirection_ = MakeVector(0.0f, 0.0f, 0.0f);
    animationSpeed_ = kPedestrianAnimationSpeed;
    const PedestrianTuning& tuning = Tuning();
    const std::int32_t rolled = Host() ? Host()->RandomInt(tuning.speedMin, tuning.speedMax) : 0;
    const float speed = static_cast<float>(rolled) * kAutomatonSpeedScale;
    SetDefaultSpeed(speed);
    SpeedTween().SetDesired(speed, 0.0f);
}

// 0x1000f3c22: the traffic automaton destructor (the pedestrian has no fields that need freeing).
cSC4Pedestrian::~cSC4Pedestrian() = default;

// ---- QueryInterface / GetGZCLSID ---------------------------------------------------------------------------------------
// 0x1000f3796: 0x896e75af and 0x8b99ee7c answer this object; everything else goes to the base.
bool cSC4Pedestrian::QueryInterface(std::uint32_t iid, void** out) {
    if (!out) return false;
    if (iid == kGZCLSID_cSC4Pedestrian) {
        *out = this;
        AddRef();
        return true;
    }
    if (iid == kGZIID_cISC4TrafficAutomaton) {
        *out = this;
        AddRef();
        return true;
    }
    return cSC4Automaton::QueryInterface(iid, out);
}

// ---- Init / Shutdown ----------------------------------------------------------------------------------------------------
// 0x1000f1792: roll the path offset (unless the occupant is already initialised), then the base Init.
bool cSC4Pedestrian::Init() {
    if (!IsInitialized()) RecalculatePathOffset();
    return cSC4TrafficAutomaton::Init();
}

// 0x1000f17f2: the base Shutdown.
bool cSC4Pedestrian::Shutdown() { return cSC4TrafficAutomaton::Shutdown(); }

// ---- movement -----------------------------------------------------------------------------------------------------------
// 0x1000f180a: a null direction moves without heading; otherwise the y of the direction is zeroed (the pedestrian walks on
// the ground plane) and the base moves.
void cSC4Pedestrian::MoveTo(const float* position, const float* direction) {
    if (!direction) {
        cSC4Automaton::MoveTo(position, nullptr);
        return;
    }
    float flattened[3] = {direction[0], 0.0f, direction[2]};
    cSC4Automaton::MoveTo(position, flattened);
}

// 0x1000f34d8: a ground-plane heading from the position to the target (the y delta is 0), or (1,0,0) when the target is on
// the position.
void cSC4Pedestrian::UpdateOrientation(const cS3DVector3& target, float* out) const {
    const float dx = target.x - position_.x;
    const float dz = target.z - position_.z;
    const float length = std::sqrt(dz * dz + dx * dx);
    if (length <= 1e-05f) {
        out[0] = 1.0f;
        out[1] = 0.0f;
        out[2] = 0.0f;
    } else {
        const float inverse = 1.0f / length;
        out[0] = dx * inverse;
        out[1] = 0.0f;
        out[2] = dz * inverse;
    }
}

// 0x1000f3568: roll the path offset in the tuning range.
void cSC4Pedestrian::RecalculatePathOffset() {
    const PedestrianTuning& tuning = Tuning();
    const double rolled = Host() ? Host()->RandomDouble(tuning.pathOffsetMin, tuning.pathOffsetMax) : 0.0;
    SetPathOffset(static_cast<float>(rolled));
}

// 0x1000f35a8: play the "idle" animation by name.
bool cSC4Pedestrian::HandleStopped() {
    cRZString name(kPedestrianIdleAnimation);
    PlayAnimByName(name);
    return true;
}

// 0x1000f3334: release the network occupant; the riot states (8/9) keep the rest of the state untouched, everything else
// runs the base handler.
void cSC4Pedestrian::HandleNetworkOccupantRemoved() {
    if (networkOccupant_) {
        ITrafficAutomatonOccupant* old = networkOccupant_;
        networkOccupant_ = nullptr;
        old->Release();
    }
    tAutomataState* state = GetState();
    if (state && (state->stateType & ~1u) == kPedestrianStateRiotLeader) return;
    cSC4TrafficAutomaton::HandleNetworkOccupantRemoved();
}

// 0x1000f117c: when the automaton does not carry flag 0x4000, record the crowd reaction state (1 or 2), move at the
// default speed and jump to the given position. Answers whether the reaction happened.
bool cSC4Pedestrian::HandleCrowdReactionEvent(const cS3DVector3& position, std::uint32_t event) {
    const bool flagged = CheckAutomataFlag(kAutomataFlagIgnoreAttractors);
    if (!flagged) {
        crowdReactionState_ = (event == 0) ? 1u : 2u;
        SetSpeed(GetDefaultSpeed(), 1.0f);
        MoveTo(&position.x, nullptr);
    }
    return !flagged;
}

// ---- the per-frame step -------------------------------------------------------------------------------------------------
// 0x1000f187a: the base tick, then when the automaton controls itself and the stopped flag just toggled, play the "idle"
// animation (HandleStopped) or the default animation (PlayAnim(0)).
bool cSC4Pedestrian::DoAnimationTick(std::uint32_t unknown, float milliseconds) {
    const bool wasStopped = CheckAutomataFlag(kTrafficFlagStopped);
    const bool result = cSC4TrafficAutomaton::DoAnimationTick(unknown, milliseconds);
    if (GetSelfControlling()) {
        if (CheckAutomataFlag(kTrafficFlagStopped) && !wasStopped) {
            HandleStopped();
        } else if (wasStopped) {
            PlayAnim(0);
        }
    }
    return result;
}

// 0x1000f338c: age the animation timer; when the flags allow and the simulator is not paused, reset the animation speed
// to 0.01 and step every model; when the first model reached its last frame, post the animation-done callback.
void cSC4Pedestrian::UpdateAnimation(float milliseconds) {
    SetAnimationTimer(AnimationTimer() + milliseconds);
    const bool blocked = CheckAutomataFlag(kAutomataFlagIgnorePaths) || CheckAutomataFlag(kAutomataFlagSelfControlling) ||
                         !CheckAutomataFlag(kTrafficFlagStopped);
    const bool allowed = Host() && Host()->AnimationUpdateAllowed();
    if (blocked && allowed) {
        animationSpeed_ = kPedestrianAnimationSpeed;
        for (IAutomatonModel* model : Models()) {
            if (!model) continue;
            model->SetAutoFrameRate(animationSpeed_);
            model->UpdateAnimation(milliseconds);
        }
        if (!Callbacks().empty() && !Models().empty()) {
            IAutomatonModel* model = Models().front();
            if (model && model->GetFrameCount() - 1 <= model->GetFrame()) SendCallbackMessage(kAutomataEventAnimationDone, 0);
        }
    }
}

// ---- the state machine --------------------------------------------------------------------------------------------------
// 0x1000f201a: the base state tick first; when it did not handle the state, dispatch on the pedestrian's own state ids.
bool cSC4Pedestrian::DoStateTick(float milliseconds) {
    if (StateStack().empty()) return false;
    if (cSC4TrafficAutomaton::DoStateTick(milliseconds)) return true;
    tAutomataState* state = GetState();
    if (!state) return false;
    switch (state->stateType) {
        case kPedestrianStateBeeLine:
            DoBeeLineTick(milliseconds);
            return true;
        case kPedestrianStateCrowd:
            return DoCrowdTick(milliseconds);
        case kPedestrianStateQueue:
            DoQueueTick(milliseconds);
            return true;
        case kPedestrianStateStrike:
            return DoStrikeTick(milliseconds);
        case kPedestrianStateRiotLeader:
            return DoRiotLeaderTick(milliseconds, false);
        case kPedestrianStateRiotFollower:
            return DoRiotFollowerTick(milliseconds);
        case kPedestrianStateIgnite:
            DoIgniteTick(milliseconds);
            return true;
        default:
            return false;
    }
}

// 0x1000f1a98: state 1 marks the automaton to ignore paths/attractors/roads; 6 self-controls and snaps to the path; 8/9/4/2/3
// run the crowd/riot flag setup; 7 is a no-op; anything else clears the ignore flags and snaps to the path.
bool cSC4Pedestrian::HandleEnterState(tAutomataState* state) {
    if (cSC4TrafficAutomaton::HandleEnterState(state)) return true;
    if (!state) {
        SetAutomataFlag(kAutomataFlagIgnorePaths, false);
        SetAutomataFlag(kAutomataFlagIgnoreAttractors, false);
        return true;
    }
    switch (state->stateType) {
        case kStateIdle:   // 1
            SetAutomataFlag(kAutomataFlagIgnorePaths, true);
            SetAutomataFlag(kAutomataFlagIgnoreAttractors, true);
            SetAutomataFlag(kAutomataFlagIgnoreRoads, true);
            return true;
        case kStateDispatched:   // 6
            SetAutomataFlag(kAutomataFlagSelfControlling, true);
            SetAutomataFlag(kAutomataFlagIgnorePaths, false);
            SnapToPath();
            return true;
        case kStateIdleTick:   // 7
            return true;
        case kPedestrianStateRiotLeader:   // 8
            SpeedTween().SetDesired(kPedestrianCrowdSpeed, 0.0f);
            SetAutomataFlag(kAutomataFlagSelfControlling, true);
            SetAutomataFlag(kAutomataFlagIgnorePaths, true);
            SetAutomataFlag(kAutomataFlagIgnoreAttractors, false);
            SetAutomataFlag(kAutomataFlagNoCollision, true);
            SetAutomataFlag(kAutomataFlagKeepAfterRoute, true);
            SetAutomataFlag(kAutomataFlagPlayerDrive, true);
            SetAutomataFlag(kAutomataFlagRemoved, true);
            DoRiotLeaderTick(0.0f, true);
            return true;
        case kPedestrianStateRiotFollower:   // 9
            SpeedTween().SetDesired(kPedestrianCrowdSpeed, 0.0f);
            [[fallthrough]];
        case kPedestrianStateQueue:   // 4
            SetAutomataFlag(kAutomataFlagSelfControlling, true);
            [[fallthrough]];
        case kPedestrianStateBeeLine:   // 2
        case kPedestrianStateCrowd:    // 3
            SetAutomataFlag(kAutomataFlagIgnorePaths, true);
            SetAutomataFlag(kAutomataFlagIgnoreAttractors, false);
            return true;
        default:
            SetAutomataFlag(kAutomataFlagIgnoreAttractors, false);
            SetAutomataFlag(kAutomataFlagIgnorePaths, false);
            SnapToPath();
            return true;
    }
}

// 0x1000f1fe0: leaving the riot-leader state clears the no-collision flag, then the base exit handler.
bool cSC4Pedestrian::HandleExitState(const tAutomataState* state) {
    if (state && state->stateType == kPedestrianStateRiotLeader) SetAutomataFlag(kAutomataFlagNoCollision, false);
    return cSC4TrafficAutomaton::HandleExitState(state);
}

// ---- the tick helpers ---------------------------------------------------------------------------------------------------
// 0x1000f2124: move along the attraction vector.
bool cSC4Pedestrian::DoBeeLineTick(float milliseconds) {
    const float length = Length(attraction_);
    if (length > 0.0f) MoveForwardWithCollision(Scale(attraction_, 1.0f / length), milliseconds);
    UpdateAttraction();
    return true;
}

// 0x1000f2de8: advance by direction * (speed * modifier * ms / |direction|), stay on the network and refuse the move when
// the target collides (the crowd-reaction direction is tried as a fallback). Answers whether the automaton moved.
bool cSC4Pedestrian::MoveForwardWithCollision(const cS3DVector3& direction, float milliseconds) {
    const float magnitude = Length(direction);
    cS3DVector3 delta = direction;
    if (magnitude > 0.0f) {
        const float scale = (GetCurrentSpeed() * GetSpeedModifier() * milliseconds) / magnitude;
        delta = Scale(direction, scale);
    }
    cS3DVector3 next = MakeVector(position_.x + delta.x, position_.y + delta.y, position_.z + delta.z);
    if (!CheckAutomataFlag(kAutomataFlagIgnoreRoads) && Host())
        Host()->PlaceOnNetwork(position_, next, kAutomataMoveNetworkMask);
    bool moved = !TestCollisionExpensive(next, milliseconds);
    if (!moved && crowdReactionDirectionSet_ && Length(crowdReactionDirection_) > 0.0f) {
        next = MakeVector(next.x + crowdReactionDirection_.x, next.y + crowdReactionDirection_.y,
                          next.z + crowdReactionDirection_.z);
        if (Host()) Host()->PlaceOnNetwork(position_, next, kAutomataMoveNetworkMask);
        moved = !TestCollisionExpensive(next, 0.0f);
    }
    crowdReactionDirectionSet_ = false;
    if (moved) MoveTo(&next.x, &direction.x);
    return moved;
}

// 0x1000f2ffa PARTIAL: the occupant-box sweep that pushes nearby pedestrians is only half recovered; the port keeps the
// timer, the 1.0-metre proximity test and the box query, but does not write the other pedestrian's reaction direction.
bool cSC4Pedestrian::TestCollisionExpensive(const cS3DVector3& position, float milliseconds) {
    collisionTimer_ += milliseconds;
    if (collisionTimer_ < 500.0f) return false;
    collisionTimer_ = 0.0f;
    if (!pedestrianHost_) return false;
    const float minXyz[3] = {position.x - 1.0f, position.y, position.z - 1.0f};
    const float maxXyz[3] = {position.x + 1.0f, position.y + 2.0f, position.z + 1.0f};
    std::vector<cISC4Occupant*> occupants;
    pedestrianHost_->OccupantsInBox(minXyz, maxXyz, kPedestrianOccupantType, 10, occupants);
    for (cISC4Occupant* occupant : occupants) {
        if (occupant == AsOccupant()) continue;
        float other[3] = {0.0f, 0.0f, 0.0f};
        occupant->GetPosition(other);
        const float dx = other[0] - position.x;
        const float dy = other[1] - position.y;
        const float dz = other[2] - position.z;
        if (std::sqrt(dx * dx + dy * dy + dz * dz) < 1.0f) return true;
    }
    return false;
}

// 0x1000f328e: true when the automaton's cell has a non-empty network tile.
bool cSC4Pedestrian::VerifyNetworkOccupant() {
    cSC4TrafficNetworkMap* map = TrafficHost() ? TrafficHost()->TrafficNetworkMap() : nullptr;
    if (!map) return false;
    TrafficTileInfo* tile = map->GetTileInfoAtCell(static_cast<int>(position_.x * 0.0625f),
                                                   static_cast<int>(position_.z * 0.0625f));
    return tile != nullptr && tile->occupant != nullptr;
}

// 0x1000f190a: the base destination choice, then re-route toward the attraction when it is far enough and a better edge
// exists.
bool cSC4Pedestrian::ChooseNewDestination() {
    if (!cSC4TrafficAutomaton::ChooseNewDestination()) return false;
    if (attractionController_ && networkOccupant_) {
        if (std::fabs(attractionDistance_) >= 20.0f) {
            const float length = Length(attraction_);
            if (length > 0.0f) attraction_ = Scale(attraction_, 1.0f / length);
            std::uint8_t edge = 0;
            GetEdgeFromVector(networkOccupant_, attraction_, edge, true);
            if (edge != static_cast<std::uint8_t>(GetPathKey())) {
                cISC4PathInfo* pathInfo = networkOccupant_->GetPathInfoObject();
                if (pathInfo) {
                    std::uint32_t key = 0;
                    if (pathInfo->GetBestPath(2, 0xff, edge, position_, &key, nullptr)) StartTrip(networkOccupant_, key, true);
                }
            }
        }
    }
    return true;
}

// ---- PARTIAL ticks (the crowd / queue / strike / riot / ignite movements) -----------------------------------------------
bool cSC4Pedestrian::DoCrowdTick(float milliseconds) {
    // 0x1000f21c2 PARTIAL: the crowd attraction, the random lookahead choice and the neighbour push are unrecovered; the
    // port moves toward the attraction like the bee-line tick.
    tAutomataState* state = GetState();
    if (!state || !state->controller) return false;
    if (Length(attraction_) > 0.0f) MoveForwardWithCollision(Scale(attraction_, 1.0f / Length(attraction_)), milliseconds);
    UpdateAttraction();
    return true;
}

bool cSC4Pedestrian::DoQueueTick(float milliseconds) {
    // 0x1000f2470 PARTIAL: the controller's queue tick (slot 0xf0) and the queue-head orientation are unrecovered; the port
    // pops an empty queue and otherwise drives a straight line.
    tAutomataState* state = GetState();
    if (!state || !state->controller) {
        PopState();
    } else {
        DoStraightLineTick(milliseconds);
    }
    UpdateAttraction();
    return true;
}

bool cSC4Pedestrian::DoStrikeTick(float milliseconds) {
    // 0x1000f2602 PARTIAL: the picket movement along the lot boundary and the re-routing are unrecovered; the port keeps
    // the snap-to-path gate and the attraction update.
    (void)milliseconds;
    if (CurrentPath() == nullptr) SnapToPath();
    UpdateAttraction();
    return true;
}

bool cSC4Pedestrian::DoRiotLeaderTick(float milliseconds, bool initialize) {
    // 0x1000f1ca8 PARTIAL: the direction choice and the collision avoidance are unrecovered; the port keeps the network
    // verification and moves toward the attraction.
    (void)initialize;
    if (!VerifyNetworkOccupant()) {
        Remove(false);
        return false;
    }
    cS3DVector3 direction = attraction_;
    if (Length(direction) <= 0.0f) direction = orientation_;
    MoveForwardWithCollision(Scale(direction, 1.0f / Length(direction)), milliseconds);
    UpdateAttraction();
    return true;
}

bool cSC4Pedestrian::DoRiotFollowerTick(float milliseconds) {
    // 0x1000f293a PARTIAL: the leader lookup and the strafe noise are unrecovered; the port moves toward the attraction.
    if (!VerifyNetworkOccupant()) {
        Remove(false);
        return false;
    }
    cS3DVector3 direction = attraction_;
    if (Length(direction) <= 0.0f) direction = orientation_;
    SpeedTween().SetDesired(kPedestrianRiotSpeed, 0.0f);
    MoveForwardWithCollision(Scale(direction, 1.0f / Length(direction)), milliseconds);
    UpdateAttraction();
    return true;
}

bool cSC4Pedestrian::DoIgniteTick(float milliseconds) {
    // 0x1000f2cb2 PARTIAL: the original asks the state's controller (slot 0x40) for the ignition point; the port uses the
    // pedestrian's own position and asks the disaster layer to ignite it (intensity 2).
    (void)milliseconds;
    for (tAutomataState* state : StateStack()) {
        if (state && state->stateType == kPedestrianStateIgnite && state->controller) {
            if (pedestrianHost_) pedestrianHost_->IgniteAt(position_, 2);
            break;
        }
    }
    return true;
}

// 0x1000f385e: the pedestrian manager (the traffic automaton's manager view).
ITrafficAutomataManager* cSC4Pedestrian::GetTrafficAutomataManager() const { return Manager(); }

// ---- persistence (stream version 5) -------------------------------------------------------------------------------------
// 0x1000f3626: version 5, then the base record, the crowd-reaction direction flag, the direction, the animation speed and
// the crowd-reaction state.
bool cSC4Pedestrian::Write(cIGZOStream& stream) const {
    stream.SetUint16(5);
    if (!cSC4TrafficAutomaton::Write(stream)) return false;
    stream.SetUint8(crowdReactionDirectionSet_ ? 1u : 0u);
    stream << crowdReactionDirection_;
    stream.SetFloat32(animationSpeed_);
    stream.SetUint32(crowdReactionState_);
    return !stream.GetError();
}

// 0x1000f36ce: version 5 only; the base record, then the same four fields.
bool cSC4Pedestrian::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 5) return false;
    if (!cSC4TrafficAutomaton::Read(stream)) return false;
    std::uint8_t flag = 0;
    stream.GetUint8(flag);
    crowdReactionDirectionSet_ = flag != 0;
    stream >> crowdReactionDirection_;
    stream.GetFloat32(animationSpeed_);
    stream.GetUint32(crowdReactionState_);
    return !stream.GetError();
}

}  // namespace sc4
