// cSC4TutorialSystem (SC4TutorialSystem.cpp in the original): the tutorial driver of a city. Hand-written from the
// Ghidra decompilation of 0x10005f0d4..0x100060145; see the header for the layout, the message routes and the quirks.
#include "sc4/TutorialSystem.h"

#include "gz/cIGZString.h"
#include "rz/RZConvert.h"
#include "rz/cRZCmdLine.h"
#include "rz/cRZString.h"
#include "sc4/TutorialTask.h"

namespace sc4 {

namespace {

ITutorialSystemHost* g_host = nullptr;
cISC4TutorialSystem* g_filterSystem = nullptr;

}  // namespace

ITutorialSystemHost* TutorialSystemHost() { return g_host; }
void SetTutorialSystemHost(ITutorialSystemHost* host) { g_host = host; }

cISC4TutorialSystem* TutorialFilterSystem() { return g_filterSystem; }
void SetTutorialFilterSystem(cISC4TutorialSystem* system) { g_filterSystem = system; }

cSC4TutorialSystem* cSC4TutorialSystem::instance_ = nullptr;

// @ 0x10005f0d4
cSC4TutorialSystem::cSC4TutorialSystem() {
    instance_ = this;
}

// @ 0x10005f1bc (alias 0x10005f2a8, deleting form 0x10005f2c0)
cSC4TutorialSystem::~cSC4TutorialSystem() {
    instance_ = nullptr;
    if (winProc_) {
        winProc_->Release();
        winProc_ = nullptr;
    }
    if (scriptingContext_) {
        cISCLua* lua = scriptingContext_;
        scriptingContext_ = nullptr;
        lua->Release();
    }
}

// @ 0x10005fa92
bool cSC4TutorialSystem::QueryInterface(std::uint32_t iid, void** out) {
    if (!out) return false;
    if (iid == kGZIID_cISC4TutorialSystem) {
        *out = static_cast<cISC4TutorialSystem*>(this);
    } else if (iid == kGZIID_cIGZSerializable) {
        *out = static_cast<cIGZSerializable*>(this);
    } else if (iid == kGZIID_cGZMessageTargetLegacy) {
        *out = static_cast<cIGZMessageTarget*>(this);
    } else if (iid == kGZIID_cIGZMessageTarget2) {
        *out = static_cast<cIGZMessageTarget2*>(this);
    } else {
        return cRZUnknown::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

// @ 0x10005fb22 (the five views share the cRZUnknown subobject's count)
std::uint32_t cSC4TutorialSystem::AddRef() { return cRZUnknown::AddRef(); }

// @ 0x10005fb64
std::uint32_t cSC4TutorialSystem::Release() { return cRZUnknown::Release(); }

// @ 0x10005f2fc
void cSC4TutorialSystem::startTutorial() {
    if (!winProc_) {
        ITutorialUIWinProc* created = g_host ? g_host->CreateTutorialUIWinProc() : nullptr;
        if (created) {
            // The original's assignment block: one reference for the member, then a second one for the Init call.
            created->AddRef();
            winProc_ = created;
            created->AddRef();
            created->Init();
        }
    }
    if (!tutorialActive_) tutorialActive_ = true;
    quitPending_ = false;
    if (g_host) g_host->TutorialRegistrySetActive(tutorialGUID_, true);
}

// @ 0x10005f3ae
void cSC4TutorialSystem::endTutorial(bool saveTutorial) {
    if (winProc_) {
        ITutorialUIWinProc* proc = winProc_;
        proc->Shutdown();
        proc->Release();
        winProc_ = nullptr;      // quirk: the original clears the member between the two releases
        proc->Release();
    }
    if (saveTutorial) {
        if (g_host) g_host->TutorialRegistrySetActive(tutorialGUID_, false);
    }
    if (g_host) {
        g_host->CloseTertiaryMenu();     // SL::spView3D vslot 0x1e0
        g_host->CloseSecondaryMenu();    // SL::spView3D vslot 0x1d8
    }
    tutorialActive_ = false;
    quitPending_ = !saveTutorial;
}

// @ 0x10005fcc8
bool cSC4TutorialSystem::Init() {
    if (!g_host) return true;
    if (cIGZMessageServer2* messages = g_host->Messages()) {
        cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
        messages->AddNotification(target, kTutorialMessageFinished);
        messages->AddNotification(target, kTutorialMessageCityViewStarted);
        messages->AddNotification(target, kTutorialMessageCityLoaded);
    }
    ITutorialCheatCodes* cheats = g_host->CheatCodes();
    if (!cheats) return true;
    if (!g_host->DebugFunctionalityEnabled()) return true;
    cheats->AddNotification(static_cast<cIGZMessageTarget*>(this), 0);
    cheats->RegisterCheatCode(kTutorialCheatId, kTutorialCheatText);
    return true;
}

// @ 0x10005fe5a
bool cSC4TutorialSystem::Shutdown() {
    if (g_host) {
        if (ITutorialCheatCodes* cheats = g_host->CheatCodes()) {
            cheats->RemoveNotification(static_cast<cIGZMessageTarget*>(this), 0);
            cheats->UnregisterCheatCode(kTutorialCheatId);
        }
        if (winProc_) {
            ITutorialUIWinProc* proc = winProc_;
            proc->Shutdown();
            proc->Release();
            winProc_ = nullptr;      // quirk: released twice, the second time through the saved pointer
            proc->Release();
        }
        if (cIGZMessageServer2* messages = g_host->Messages()) {
            cIGZMessageTarget2* target = static_cast<cIGZMessageTarget2*>(this);
            messages->RemoveNotification(target, kTutorialMessageFinished);
            messages->RemoveNotification(target, kTutorialMessageCityViewStarted);
            messages->RemoveNotification(target, kTutorialMessageCityLoaded);
        }
    }
    return true;
}

// @ 0x10005f452
bool cSC4TutorialSystem::DoMessage(cIGZMessage2* message) {
    if (!message) return true;
    auto* standard = static_cast<cIGZMessage2Standard*>(message);
    switch (message->GetType()) {
        case kTutorialMessageFinished:
            if (winProc_) endTutorial(standard->GetData1() == 0);
            break;
        case kTutorialMessageCityLoaded:
            if (quitPending_) endTutorial(true);
            break;
        case kTutorialMessageCityViewStarted:
            if (!cityViewHandled_ && tutorialActive_) {
                if (g_host && g_host->TutorialItemActive(tutorialGUID_)) startTutorial();
                cityViewHandled_ = true;
            }
            break;
        default:
            break;
    }
    return true;
}

// @ 0x10005f528
bool cSC4TutorialSystem::DoMessage(cGZMessage& message) {
    if (message.type != kTutorialMessageCheatCode) return true;
    // The original reads the typed text out of the message's second parameter without a null check.
    auto* text = reinterpret_cast<cIGZString*>(static_cast<std::uintptr_t>(message.param2));
    cRZCmdLine cmdLine(text ? text->ToChar() : "");
    if (message.param1 != kTutorialCheatId) return true;
    const int argc = cmdLine.argc();
    if (argc == 3) {
        cRZString verb(cmdLine[1]);
        verb.MakeLower();
        if (verb.str() == kTutorialCheatReset) {
            const int guid = static_cast<int>(RZ::ConvertToUint(cmdLine[2].ToChar()));
            if (g_host) {
                g_host->ResetTutorialCity(guid);
                startTutorial();
                ResetTutorialNumAndIndex(guid, 0);
            }
        } else if (verb.str() == kTutorialCheatSave) {
            const std::uint32_t guid = RZ::ConvertToUint(cmdLine[2].ToChar());
            if (guid + 1u >= 2u) {          // the original refuses 0 and 0xffffffff, nothing else
                tutorialActive_ = true;
                tutorialGUID_ = static_cast<std::int32_t>(guid);
                taskNumber_ = 0;
                quitPending_ = false;
                if (g_host) {
                    g_host->SetRegionalCityTutorialGUID(static_cast<int>(guid));
                    g_host->RequestSaveCity();
                }
            }
        } else if (verb.str() == kTutorialCheatStep) {
            const int index = static_cast<int>(RZ::ConvertToUint(cmdLine[2].ToChar()));
            ResetTutorialNumAndIndex(tutorialGUID_, index);
        }
    } else if (argc == 2) {
        cRZString verb(cmdLine[1]);
        verb.MakeLower();
        if (verb.str() == kTutorialCheatSave) {
            // quirk: this branch dereferences SL::spSC4 without a null check in the original
            if (g_host) {
                g_host->SetRegionalCityTutorialGUID(tutorialGUID_);
                g_host->RequestSaveCity();
            }
        } else if (verb.str() == kTutorialCheatNext) {
            if (winProc_) winProc_->moveToNext();
        } else if (verb.str() == kTutorialCheatQuit) {
            if (winProc_) endTutorial(false);
        } else if (verb.str() == kTutorialCheatReset) {
            if (g_host) {
                g_host->ResetTutorialCity(tutorialGUID_);
                startTutorial();
                ResetTutorialNumAndIndex(tutorialGUID_, 0);
            }
        }
    }
    return true;
}

// @ 0x10005fba6
bool cSC4TutorialSystem::Write(cIGZOStream& stream) const {
    if (!stream.GetError()) {
        stream.SetUint16(kTutorialStreamVersion);
        stream.SetUint8(tutorialActive_ ? 1 : 0);
        stream.SetSint32(taskNumber_);
        stream.SetSint32(tutorialGUID_);
    }
    return !stream.GetError();
}

// @ 0x10005fc1a
bool cSC4TutorialSystem::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != kTutorialStreamVersion) return false;
    if (!stream.GetError()) {
        std::uint8_t active = 0;
        stream.GetUint8(active);
        tutorialActive_ = active != 0;
        stream.GetSint32(taskNumber_);
        stream.GetSint32(tutorialGUID_);
        if (g_host) tutorialGUID_ = g_host->TutorialGUIDFromStoredValue(tutorialGUID_);
    }
    return !stream.GetError();
}

// @ 0x10005ff0c
bool cSC4TutorialSystem::FilterMessage(cGZMessage& message) {
    if (message.type != kGZMsgCommand) return true;
    if (message.param1 == kTutorialCommandCatalogItem) {
        if (winProc_) winProc_->handleButtons(static_cast<std::uint32_t>(message.param3));
    } else if (message.param1 == kTutorialCommandButtonChanged ||
               message.param1 == kTutorialCommandButtonActivated) {
        if (winProc_) winProc_->handleButtons(static_cast<std::uint32_t>(message.param2));
    }
    return true;
}

// @ 0x10005ff56
bool cSC4TutorialSystem::ResetTutorialNumAndIndex(int guid, int index) {
    if (!winProc_) return false;
    const int taskNumber = winProc_->ResetTutorialNumAndIndex(guid, index);
    tutorialGUID_ = guid;
    taskNumber_ = taskNumber;
    return true;
}

// @ 0x10005ff88
void cSC4TutorialSystem::ShowUIHintsForUsers() {
    if (winProc_) winProc_->ShowUIHintsForUsers();
}

// @ 0x10005ff9e
bool cSC4TutorialSystem::FilterLeftMouseClick(int& x, int& y, int flags) {
    if (winProc_) return winProc_->FilterLeftMouseClick(x, y, flags);
    return true;
}

// @ 0x10005ffba
bool cSC4TutorialSystem::HandleCatalogMenu(cISC4WinCatalogView* view) {
    if (winProc_) winProc_->HandleCatalogView(view);
    return true;
}

// @ 0x10005ffd2
cISCLua* cSC4TutorialSystem::GetScriptingContext() {
    if (!scriptingContext_) {
        cISCLua* lua = g_host ? g_host->ScriptingContext() : nullptr;
        cISCLua* previous = scriptingContext_;
        if (previous != lua) {
            if (lua) lua->AddRef();
            scriptingContext_ = lua;
            if (previous) previous->Release();
        }
    }
    return scriptingContext_;
}

// @ 0x100060042 (the original answers true after forwarding the event)
bool cSC4TutorialSystem::OnTutorialEvent(eTutorialEvent event) {
    if (winProc_) winProc_->OnTutorialEvent(event);
    return true;
}

// @ 0x100060058
bool cSC4TutorialSystem::IsEdgeScrollingAllowed() const {
    if (!winProc_) return true;
    cISC4TutorialTask* task = winProc_->GetCurrentTask();
    if (!task) return true;
    return task->IsEdgeScrollingAllowed();
}

// @ 0x1000600e0
bool cSC4TutorialSystem::IsTutorial() const { return tutorialActive_; }

// @ 0x1000600ee
cISC4TutorialTask* cSC4TutorialSystem::GetCurrentTask() const {
    if (!winProc_) return nullptr;
    return winProc_->GetCurrentTask();
}

// @ 0x100060106
std::int32_t cSC4TutorialSystem::GetTutorialTaskNumber() const { return taskNumber_; }

// @ 0x100060110
std::int32_t cSC4TutorialSystem::GetTutorialGUID() const { return tutorialGUID_; }

// @ 0x10006011a
std::uint32_t cSC4TutorialSystem::GetGZCLSID() const { return kGZCLSID_cSC4TutorialSystem; }

// @ 0x100060126
void cSC4TutorialSystem::ResetTutorialIndex(int index) { taskNumber_ = index; }

// @ 0x100060130
void cSC4TutorialSystem::IncreaseTutorialNumber() { ++taskNumber_; }

// 0x10006008e
bool FilterUICommandMessage(cGZMessage& message) {
    cISC4TutorialSystem* system = g_filterSystem;
    if (!system) return true;
    if (!system->IsTutorial()) return true;
    return system->FilterMessage(message);
}

}  // namespace sc4
