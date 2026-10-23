#include "sc4/PropOccupantAutomataController.h"

#include <cstring>

#include "gz/StreamOps.h"
#include "sc4/SCPropertyHelp.h"

namespace sc4 {

namespace {
IPropControllerHost* g_host = nullptr;
constexpr std::uint16_t kStreamVersion = 3;
}

IPropControllerHost* PropControllerHost() { return g_host; }
void SetPropControllerHost(IPropControllerHost* host) { g_host = host; }

cSC4PropOccupantAutomataController::~cSC4PropOccupantAutomataController() {
    if (controller_) controller_->Release();
}

bool cSC4PropOccupantAutomataController::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZCLSID_cSC4PropOccupantAutomataController || iid == kGZIID_cISC4PropOccupantAutomataController) {
        *out = static_cast<cISC4PropOccupantAutomataController*>(this);
        AddRef();
        return true;
    }
    return cSC4PropOccupant::QueryInterface(iid, out);
}

bool cSC4PropOccupantAutomataController::Init() {
    if (IsInitialized()) return true;
    const cISCPropertyHolder* props = AsPropertyHolder();
    std::vector<float> size;
    float box[3] = {16.0f, 16.0f, 16.0f};
    if (cSCPropertyHelp::GetPropertyValue(props, kPropertyPropBoxSize, size) && size.size() >= 3) {
        box[0] = size[0]; box[1] = size[1]; box[2] = size[2];
    }
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    SetBoundingBox(zero, box);
    cSC4PropOccupant::SetPosition(zero);
    cRZString name;
    if (!cSCPropertyHelp::GetPropertyValue(props, kPropertyControllerTemplate, name)) {
        cSCPropertyHelp::GetPropertyValue(props, kPropertyExemplarName, name);
    }
    if (name.Strlen() != 0) {
        SetControllerTemplate(name.ToChar());
        active_ = true;
    }
    if (!IsInitialized()) cSC4Occupant::Init();
    if (active_ && controller_ && (flags_ & kOccupantFlagBit2) != 0) controller_->Activate(true);
    return true;
}

bool cSC4PropOccupantAutomataController::Shutdown() {
    if (IsInitialized()) {
        if (controller_) {
            if (g_host) g_host->RemoveController(controller_);
            cSC4AutomataController* old = controller_;
            controller_ = nullptr;
            old->Release();
        }
        cSC4PropOccupant::Shutdown();
    }
    return true;
}

bool cSC4PropOccupantAutomataController::Write(cIGZOStream& stream) const {
    stream.SetUint16(kStreamVersion);
    if (cSC4PropOccupant::Write(stream)) {
        stream << active_;
        stream.SetGZStr(templateName_);
    }
    return !stream.GetError();
}

bool cSC4PropOccupantAutomataController::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != kStreamVersion) return false;
    if (cSC4PropOccupant::Read(stream)) {
        stream >> active_;
        stream.GetGZStr(templateName_);
    }
    return !stream.GetError();
}

void cSC4PropOccupantAutomataController::SetState(std::uint32_t state) {
    if (GetState() == static_cast<std::uint8_t>(state)) return;
    cSC4PropOccupant::SetState(state);   // see PROPCTRL-1: the original only stores the byte
    RestartController();
}

void cSC4PropOccupantAutomataController::RestartController() {
    const bool wasActive = active_;
    IPropControllerHost* host = g_host;
    if (controller_) {
        controller_->Activate(false);
        if (host->HasControllerManager()) host->RemoveController(controller_);
        cSC4AutomataController* old = controller_;
        controller_ = nullptr;
        old->Release();
    }
    if (templateName_.Strlen() == 0) return;
    cSC4AutomataController* created;
    if (GetState() == 0) {
        created = host->CreateController(static_cast<cISC4Occupant*>(this), templateName_);
    } else {
        cRZString stateName;
        stateName.Sprintf("%s_%d", templateName_.ToChar(), static_cast<int>(GetState()));
        created = host->CreateController(static_cast<cISC4Occupant*>(this), stateName);
    }
    if (controller_ != created) {
        cSC4AutomataController* old = controller_;
        if (created) created->AddRef();
        controller_ = created;
        if (old) old->Release();
    }
    ActivateController(wasActive);
}

void cSC4PropOccupantAutomataController::SetControllerTemplate(const char* name) {
    if (templateName_.Strlen() == std::strlen(name) && std::strcmp(templateName_.ToChar(), name) == 0) return;
    templateName_.AssignFromCString(name);
    RestartController();
}

void cSC4PropOccupantAutomataController::ActivateController(bool active) {
    if (controller_) controller_->Activate(active);
    active_ = active;
}

}  // namespace sc4
