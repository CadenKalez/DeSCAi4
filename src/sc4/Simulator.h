// cSC4Simulator — the simulation clock and agent scheduler (52 + thunks, 0x1001ef29e..0x1001f1a50; calibrator
// 0x10070e58e/0x10070e5e4). Service id 0x46956659, priority -2,000,000, class id 0x2990c1e5, message target 0x452294aa,
// serializable view 0xe4fda3d4.
//
// Agents are message targets that want a message at a fixed point of the simulated calendar. They live in eleven sets
// (one per interval, in the order of their message 0x66956810 + interval): 0 and 1 are "every tick" sets, 2 minute,
// 3 hour, 4 day (0x66956814), 5 week (0x66956815), 6 month (0x66956816), 7 year (0x66956817), 8/9 extra, 10 idle time
// (0x6695681a). Time only advances (SimulateTimePassage) when the sets of the boundaries it crosses have finished their
// round; SimulateAgents then walks the sets with a per-tick millisecond budget. Sim speeds 0..2 (slow, medium, fast)
// pace one simulated day per 5000 / 1000 / 100 ms; speed 3 is the start-up state. Pause counters: normal (+0x48), hidden
// (+0x4c) and emergency (+0x50).
// Layout: +0x44 started, +0x45 inside OnTick, +0x46 city established, +0x54 speed, +0x58 sim time, +0x5c emergency
// resume allowed, +0x90 millisecond budget per tick, +0x94 pace interval (4 = day), +0x98 ms per interval, +0x9c target
// frame rate, +0xc8 interval counters[11], +0xf4 pending flags[11], +0x100 date, +0x120 message server, +0x128 pace table,
// +0x138 animation dilation table, +0x150 agent sets.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "gz/CommandParameterSet.h"
#include "gz/cGZDate.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZSerializable.h"
#include "rz/cRZSystemService.h"
#include "sc4/Occupant.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4Simulator = 0x2990c1e5;
constexpr std::uint32_t kGZIID_cISC4Simulator = 0x8695664e;
constexpr std::uint32_t kSID_cSC4Simulator = 0x46956659;
constexpr std::uint32_t kMessageSimInterval = 0x66956810;         // + interval index
constexpr std::uint32_t kMessageSimSpeedChanged = 0x4a7fb7e9;
constexpr std::uint32_t kMessagePauseChanged = 0xaa7fb7e0, kMessageHiddenPauseChanged = 0x4a7fb7e2, kMessageEmergencyPauseChanged = 0x4a7fb807;
constexpr std::uint32_t kCommandGetSimulationDate = 0x8bac4b43;
constexpr std::uint32_t kCheatSetTargetFrameRate = 0x0ac07e24, kCheatSimulatorDate = 0x8a78beff;
constexpr int kSimIntervalCount = 11;
constexpr int kIntervalIdle = 10;

class cSC4LimitTimerCalibrator {
public:
    cSC4LimitTimerCalibrator();                // @ 0x10070e58e
    void Tick();                               // @ 0x10070e5e4
    float cyclesToMilliseconds;                // +0
private:
    std::uint64_t lastMachTime_ = 0, lastTimerCycle_ = 0;      // +8, +0x10
    std::uint64_t minimumCycles_;              // +0x18
    float millisecondsPerSecond_;              // +0x20
};

// What the simulator needs from the running game.
class ISimulatorHost {
public:
    virtual ~ISimulatorHost() = default;
    virtual cIGZMessageServer2* Messages() = 0;                                 // SL::spMS2
    virtual void RegisterService(cIGZSystemService* service) = 0;              // framework AddSystemService + AddToTick
    virtual void UnregisterService(cIGZSystemService* service) = 0;
    virtual void RegisterCheat(std::uint32_t id, const char* text) = 0;        // cheat code manager
    virtual void UnregisterCheat(std::uint32_t id) = 0;
    virtual void AddCheatNotification(cIGZMessageTarget2* target, std::uint32_t id) = 0;
    virtual void RemoveCheatNotification(cIGZMessageTarget2* target, std::uint32_t id) = 0;
    virtual void RegisterCommand(std::uint32_t id, const char* name) = 0;      // command server
    virtual void UnregisterCommand(std::uint32_t id) = 0;
    virtual bool DebugFunctionalityEnabled() = 0;                              // SL::spSC4 vslot 0xa8
    virtual bool IsDrivingMode() = 0;                                          // SL::spVehicleManager vslot 0x68
    virtual cISCPropertyHolder* TuningExemplar() = 0;                          // exemplar 0x6534284a / 0xe7e2c2db / 0xe9590e52
};
ISimulatorHost* SimulatorHost();
void SetSimulatorHost(ISimulatorHost* host);

class cISC4Simulator : public cIGZUnknown {};

class cSC4Simulator : public cISC4Simulator, public cIGZSerializable, public cRZSystemService, public cIGZMessageTarget2 {
public:
    struct AgentInfo {
        cIGZMessageTarget2* agent = nullptr;       // +0x20 of the tree node (the key)
        bool enabled = true;                       // +0x28
        std::string name;                          // +0x30
        std::uint32_t flags = 0;                   // +0x58 (1 = runs before the city is established, 2 = after)
    };

    cSC4Simulator();
    ~cSC4Simulator() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }
    std::uint32_t Release() override { return cRZSystemService::Release(); }
    bool Init() override;
    bool Shutdown() override;
    bool OnTick(std::uint32_t elapsedMs) override;
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4Simulator; }
    bool DoMessage(cIGZMessage2* message) override;

    // pausing and speed
    void Pause();
    void HiddenPause();
    void EmergencyPause();
    void Resume();
    void HiddenResume();
    void EmergencyResume();
    bool IsPaused() const { return pauseCount_ > 0; }
    bool IsHiddenPaused() const { return hiddenPauseCount_ > 0; }
    bool IsEmergencyPaused() const { return emergencyPauseCount_ > 0; }
    bool IsAnyPaused() const { return pauseCount_ > 0 || hiddenPauseCount_ > 0 || emergencyPauseCount_ > 0; }
    bool SetSimSpeed(int speed);
    int GetSimSpeed() const { return speed_; }
    void SetSimTime(int time) { simTime_ = time; }
    int GetSimTime() const { return simTime_; }
    void SetMaxMillisecondsPerTick(std::uint32_t ms) { budgetMs_ = ms; }
    float GetAnimationTimeDilation();

    // calendar
    const cGZDate& GetSimDate() const { return date_; }
    std::uint32_t GetSimDateNumber() const { return date_.DayNumber(); }
    void GetSimDate(int* year, int* month, int* dayOfMonth, int* dayOfYear, int* weekDay) const;
    static void GetSimStartDateStatic(cIGZDate& out) { out.Set(1u, 1u, 2000u); }
    void GetSimStartDate(cIGZDate& out) const { GetSimStartDateStatic(out); }

    // agents (`interval` 0..10)
    bool AddAgent(cIGZMessageTarget2* agent, int interval, const cIGZString& name, std::uint32_t flags);
    bool RemoveAgent(cIGZMessageTarget2* agent, int interval);
    bool RemoveAgent(cIGZMessageTarget2* agent);
    bool RemoveAllAgents(int interval);
    bool RemoveAllAgents();
    void EnumerateAgentsByName(std::vector<std::string>& names) const;
    bool GetAgentEnabled(const cIGZString& name) const;
    void SetAgentEnabled(const cIGZString& name, bool enabled);
    void SetCityEstablished(bool established);
    bool IsCityEstablished() const { return cityEstablished_; }

    // internals, exposed for tests
    void ReadSimulatorTunableValues();
    void DoSetSimSpeed(int speed);
    void SimulateTimePassage();
    void SimulateAgents();
    void SimulateIdleTime();
    void BeginNewInterval(int interval);
    void DoCheatCode(std::uint32_t id, const cIGZString* text);
    void PostMessageServerMessage(std::uint32_t type, std::uint32_t data1, std::uint32_t data2, std::uint32_t data3);
    bool SendAgentMessage(const AgentInfo& agent, std::uint32_t type, std::uint32_t data1, std::uint32_t data2, std::uint32_t data3);
    std::uint32_t Counter(int interval) const { return counters_[interval]; }
    bool Pending(int interval) const { return pending_[interval]; }
    std::size_t AgentCount(int interval) const { return sets_[interval].agents.size(); }
    void SetClockForTest(std::function<std::uint32_t()> clock) { clock_ = std::move(clock); }

private:
    using AgentMap = std::map<cIGZMessageTarget2*, AgentInfo>;
    struct AgentSet {
        AgentMap agents;                           // ordered by agent pointer
        AgentMap::iterator cursor;                 // the next agent of this round
    };
    AgentMap::iterator Next(AgentSet& set, AgentMap::iterator it) { return ++it; }
    std::uint32_t Now() const;

    bool started_ = false;                         // +0x44
    bool inTick_ = false;                          // +0x45
    bool cityEstablished_ = false;                 // +0x46
    int pauseCount_ = 0, hiddenPauseCount_ = 0, emergencyPauseCount_ = 0;   // +0x48, +0x4c, +0x50
    int speed_ = 3;                                // +0x54
    int simTime_ = 1;                              // +0x58
    bool emergencyResumeAllowed_ = true;           // +0x5c
    std::uint32_t paceStart_ = 0, tickStart_ = 0, budgetStart_ = 0;   // the three millisecond timers (+0x60, +0x78, +0x258)
    std::uint32_t budgetMs_ = 15;                  // +0x90
    int paceInterval_ = 4;                         // +0x94
    std::uint32_t msPerInterval_ = 100;            // +0x98
    int targetFrameRate_ = 15;                     // +0x9c
    cSC4LimitTimerCalibrator calibrator_;          // +0xa0
    std::uint32_t counters_[kSimIntervalCount] = {};     // +0xc8
    bool pending_[kSimIntervalCount] = {};         // +0xf4
    cGZDate date_{1, 1, 2000};                     // +0x100
    cIGZMessageServer2* messages_ = nullptr;       // +0x120
    std::uint32_t paceTable_[3] = {5000, 1000, 100};          // +0x128
    std::vector<float> dilation_ = {0, 1.0f, 1.5f, 2.0f, 1.0f};   // +0x138
    AgentSet sets_[kSimIntervalCount];             // +0x150
    std::function<std::uint32_t()> clock_;
};

}  // namespace sc4
