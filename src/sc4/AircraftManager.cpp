#include "sc4/AircraftManager.h"

#include "sc4/DBSegment.h"

namespace sc4 {

cSC4AircraftManager::cSC4AircraftManager() {
    SetExemplarGroup(kAircraftManagerExemplarGroup);
    SetDefaultGroup(kAircraftManagerDefaultGroup);
    SetFallbackGroup(kAircraftManagerFallbackGroup);
}

cSC4AircraftManager::~cSC4AircraftManager() {
    for (auto& entry : airports_) {
        if (entry.first) entry.first->Release();
        if (entry.second) entry.second->Release();
    }
}

bool cSC4AircraftManager::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZCLSID_cSC4AircraftManager) {
        *out = static_cast<cSC4AircraftManager*>(this);
        AddRef();
        return true;
    }
    return cSC4AutomataManager::QueryInterface(iid, out);
}

bool cSC4AircraftManager::Init() {
    if (!Initialized()) {
        cSC4AutomataManager::Init();
        HandleTuningParametersChanged();
    }
    return true;
}

bool cSC4AircraftManager::Shutdown() {
    if (Initialized()) {
        for (auto& entry : airports_) {
            if (entry.first) entry.first->Release();
            if (entry.second) entry.second->Release();
        }
        airports_.clear();
        runways_.clear();
        cSC4AutomataManager::Shutdown();
    }
    return true;
}

bool cSC4AircraftManager::Write(cIGZOStream& stream) const {
    stream.SetUint16(kAircraftManagerRecordVersion);
    if (cSC4AutomataManager::Write(stream)) {
        stream.SetUint32(static_cast<std::uint32_t>(airports_.size()));
        for (const auto& entry : airports_) {
            SC4WriteGZSerializable(stream, entry.first);
            SC4WriteGZSerializable(stream, entry.second);
        }
        stream.SetUint32(static_cast<std::uint32_t>(runways_.size()));
        for (const AircraftRunwayMarker& marker : runways_) {
            stream.SetFloat32(marker.x);
            stream.SetFloat32(marker.y);
            stream.SetFloat32(marker.z);
            stream.SetUint8(marker.flag);
        }
    }
    return !stream.GetError();
}

bool cSC4AircraftManager::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version < kAircraftManagerRecordVersion) return ReadInternal(stream, version);
    if (version != kAircraftManagerRecordVersion) return false;
    if (cSC4AutomataManager::Read(stream)) {
        std::uint32_t count = 0;
        stream.GetUint32(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            void* occupant = nullptr;
            void* automaton = nullptr;
            SC4ReadGZSerializable(stream, kAircraftIIDOccupant, &occupant, false);
            SC4ReadGZSerializable(stream, kAircraftIIDAutomaton, &automaton, false);
            airports_[static_cast<cIGZUnknown*>(occupant)] = static_cast<cIGZUnknown*>(automaton);
        }
        stream.GetUint32(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            AircraftRunwayMarker marker;
            stream.GetFloat32(marker.x);
            stream.GetFloat32(marker.y);
            stream.GetFloat32(marker.z);
            stream.GetUint8(marker.flag);
            runways_.push_back(marker);
        }
    }
    return !stream.GetError();
}

}  // namespace sc4
