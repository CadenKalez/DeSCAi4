// cSC4App: the city life cycle — new / load / close / save requests, quit, the application record of a city file, the
// application's message handlers and the city message helper (0x100011a14..0x100011f14, 0x100044510..0x1000475cc).
//
// Everything the original reaches through unrecovered neighbours (dialogs, the region screens, the regional city records,
// the city file segment, the busy cursor) goes through cityHooks_. Quirks kept:
//  * DoLoadCityUI fetches the application directory and then overwrites it: the default load path is "SavedCity.sc4".
//  * RequestQuit / RequestGoToRegionView show a different dialog when saving is disabled, and only the "OK" answer
//    (0xea57da59) of that dialog goes on.
//  * Quit and the "go to region" request flush the graphics system twice before the city is closed.
//  * PartialCloseCity pauses the game (all but graphics) and never resumes it itself.
#include <cstring>

#include "gz/RZServices.h"
#include "rz/cRZFile.h"
#include "rz/cRZKeyboard.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZVariant.h"
#include "sc4/App.h"
#include "sc4/AutoHelpers.h"
#include "sc4/DBSegment.h"
#include "sc4/ServiceLocator.h"

bool cSC4App::sMacQuitDialogIsShowing_ = false;

namespace {
constexpr std::uint32_t kUITextGroup = 0x6a231eaa;
constexpr std::uint32_t kKeyMenu = 0x12;                                // VK_MENU (Alt)
constexpr std::uint32_t kDialogNo = 0x8a921f65, kDialogYes = 0x8a921f5b, kDialogQuitRegionNo = 0x8a921f70, kDialogOk = 0xea57da59;
constexpr std::uint32_t kMessageQuit = 0x29a89b05;
constexpr std::uint32_t kCityRecordIid = 0x66ba4db5;

// cSLString(group, instance) through the hook
std::string Text(sc4::AppCityHooks& hooks, std::uint32_t instance) { return hooks.loadText ? hooks.loadText(kUITextGroup, instance) : std::string(); }

void Notify(sc4::AppCityHooks& hooks, std::uint32_t titleInstance, std::uint32_t textInstance, const std::string& suffix = std::string()) {
    if (hooks.notify) hooks.notify(Text(hooks, titleInstance), Text(hooks, textInstance) + suffix);
}

struct BusyScope {
    sc4::AppCityHooks& hooks;
    explicit BusyScope(sc4::AppCityHooks& h) : hooks(h) { if (hooks.busy) hooks.busy(true); }
    ~BusyScope() { if (hooks.busy) hooks.busy(false); }
};

void SendMessage(cIGZMessage2& message) { if (SL::spMS2) SL::spMS2->MessageSend(&message); }
}  // namespace

// ---- messages ---------------------------------------------------------------------------------------------------

void cSC4App::PostCityMessage(cISC4City* city, std::uint32_t type, std::uint32_t data2, std::uint32_t data3, bool post) {
    if (!SL::spMS2) return;
    cRZAutoRefCount<cRZMessage2Standard> message(new cRZMessage2Standard());
    message->SetType(type);
    message->SetVoid1(city);
    message->SetIGZUnknown(city);
    message->SetData2(data2);
    message->SetData3(data3);
    if (post) SL::spMS2->MessagePost(message.get(), false);
    else SL::spMS2->MessageSend(message.get());
}

// File change notices re-scan the plugin folders; the cheat-code message handles the pause-on-focus-loss cheat and the
// "next stage" cheat; unknown cheats are not handled.
bool cSC4App::DoMessage(cGZMessage& message) {
    if (message.type == 0x87163754) {
        const void* source = reinterpret_cast<const void*>(message.param1);
        if (source == &pluginWatcher_ || source == &dataWatcher_) {
            cRZString directory;
            static_cast<const FileChangeNotification*>(source)->GetWatchedDirectory(directory);
            if (AddCodePlugins(directory) != 0 && SL::spMS2) {
                cRZAutoRefCount<cRZMessage2Standard> reply(new cRZMessage2Standard());
                reply->SetType(0xa42396c6);
                SL::spMS2->MessagePost(reply.get(), false);
            }
        }
    } else if (message.type == 0x230e27ac) {
        if (message.param1 == 0x8b6d4f8d) {
            if (SL::spMS2) {
                cRZAutoRefCount<cRZMessage2Standard> reply(new cRZMessage2Standard());
                reply->SetType(0xba44eb51);
                reply->SetData1(0x3c);
                SL::spMS2->MessagePost(reply.get(), false);
            }
        } else if (message.param1 == 0x2ab5c431) {
            const cIGZString* text = reinterpret_cast<const cIGZString*>(message.param2);
            cRZCmdLine line(text->ToChar());
            bool enable = !pauseOnFocusLoss_;
            if (line.argc() > 1) {
                cRZString option(line[1].ToChar());
                option.MakeLower();
                if (option.IsEqualToChars("on", 2, true)) enable = true;
                else if (option.IsEqualToChars("off", 3, true)) enable = false;
            }
            if (enable != pauseOnFocusLoss_) EnableFullGamePauseOnAppFocusLoss(enable);
        } else if (message.param1 != 0x2b60e45b) {
            return false;
        }
    } else if (message.type == 0xc7aa61c6) {
        RequestQuit(false, false);
    }
    return true;
}

bool cSC4App::DoMessage(cIGZMessage2* message) {
    switch (message->GetType()) {
        case 0xea65224e: DoCityObliteration(); break;
        case 0xea8ae29a: appState_ = 2; break;
        case 0xcbb5bb45: appState_ = 1; break;
        case 0x8bb5bb46:
        case 0x26d31ec2: appState_ = 0; break;
        case 0xfa0c6390:
        case 0x7a104750: timer428_.Restart(); break;
        case 0xba44eb51: if (SL::spRM) SL::spRM->ForceGarbageCollection(); break;
        case 0x0b627ad3:
        case 0x49a780b2: LaunchExceptionReportUploader(); break;
        case 0x231bbf91: DoPreferencesChanged(false); break;
        case kMessageQuit: Quit(); break;
        default: break;
    }
    return true;
}

// ---- the application record --------------------------------------------------------------------------------------

// Record version 2: the city object, then (through the regional city holder) the regional city records.
bool cSC4App::Write(cIGZOStream& stream, cIGZPersistDBSegment* segment) const {
    stream.SetUint16(2);
    if (!sc4::SC4WriteGZSerializable(stream, city_)) return false;
    if (regionalCity_ && regionalCity_->get()) return cityHooks_.writeRegionalCity && cityHooks_.writeRegionalCity(segment, regionalCity_->get());
    return true;
}

bool cSC4App::Read(cIGZIStream& stream, cIGZPersistDBSegment* segment) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 2) return false;
    void* city = nullptr;
    if (!sc4::SC4ReadGZSerializable(stream, kCityRecordIid, &city, false)) return false;
    city_ = static_cast<cISC4City*>(city);
    cRZAutoRefCount<cISC4RegionalCity> loaded;
    if (cityHooks_.readRegionalCity && cityHooks_.readRegionalCity(segment, loaded) && regionalCity_) {
        if (regionalCity_->get()) regionalCity_->get()->Shutdown();
        *regionalCity_ = loaded;
    }
    return true;
}

// ---- life cycle -----------------------------------------------------------------------------------------------

bool cSC4App::RequestNewCity(cRZAutoRefCount<cISC4RegionalCity>* holder) {
    if (city_) {
        RequestCloseCity(true);
        if (city_) return false;
    }
    regionalCity_ = holder;
    return NewCity();
}

bool cSC4App::DoLoadCityUI(cRZString& path) {
    RZ::ADir(path);
    path.AssignFromCString("SavedCity");
    path.AppendChars(".sc4", 4);
    return true;
}

bool cSC4App::RequestLoadCity() {
    if (city_) {
        RequestCloseCity(true);
        if (city_) return false;
    }
    cRZString path;
    DoLoadCityUI(path);
    const bool loaded = LoadCity(path, nullptr);
    if (!loaded) Notify(cityHooks_, 0xca56ce8c, 0x4a56cea6);
    return loaded;
}

bool cSC4App::RequestCloseCity(bool confirmFirst) {
    sc4::cSC4AutoGamePause pause(false, true, true, true);
    if (!city_) return false;
    if (confirmFirst) {
        const bool answeredYes = cityHooks_.confirm && cityHooks_.confirm(Text(cityHooks_, 0x2a56cda8), Text(cityHooks_, 0x2a56cd9b));
        if (answeredYes) {
            const bool saved = RequestSaveCity(true, cRZKeyboard::IsKeyDownNowStatic(kKeyMenu));
            return saved ? CloseCity() : false;
        }
    }
    return CloseCity();
}

bool cSC4App::RequestSaveCity(bool announce, bool alternate) {
    sc4::cSC4AutoGamePause pause(false, true, true, true);
    if (!city_) return false;
    if (city_->IsSaveDisabled()) {
        Notify(cityHooks_, 0xaa666afd, 0xca666b15);
        return false;
    }
    cRZString path;
    city_->GetCitySaveFilePath(path);
    if (path.Strlen() == 0) return false;
    const bool saved = SaveCity(path, alternate);
    city_->GetCitySaveFilePath(path);
    cRZString name;
    cRZFile::GetPathName(path, name, true);
    const std::string suffix = std::string(" (") + name.ToChar() + ")";
    if (!saved) Notify(cityHooks_, 0x2a56cda8, 0x4a56ce71, suffix);
    else if (announce) Notify(cityHooks_, 0x2a56cda8, 0x2a56ce36, suffix);
    return saved;
}

bool cSC4App::SaveCity(bool alternate) {
    if (!city_) return false;
    cRZString path;
    city_->GetCitySaveFilePath(path);
    if (path.Strlen() == 0) return false;
    return SaveCity(path, alternate);
}

// The dialogs of RequestQuit and RequestGoToRegionView. Returns true when the request goes on.
bool cSC4App::RequestQuit(bool askPlayer, bool saveFirst) {
    sc4::cSC4AutoGamePause pause(false, true, true, true);
    auto scripted = [&](std::uint32_t script, std::uint32_t window) { return cityHooks_.scriptedModal ? cityHooks_.scriptedModal(script, window) : 0; };
    if (!city_ || !city_->IsSaveDisabled()) {
        std::uint32_t answer;
        if (askPlayer) answer = static_cast<std::uint32_t>(scripted(0x0a55161d, 0xaa921f4f));
        else answer = saveFirst ? kDialogYes : kDialogNo;
        if (answer != kDialogNo) {
            if (answer != kDialogYes) return false;
            RunMessageServerPump(100, 500, 5000);
            RunMessageServer2Pump(100, 500, 5000);
            RequestSaveCity(false, cRZKeyboard::IsKeyDownNowStatic(kKeyMenu));
        }
    } else if (askPlayer) {
        if (static_cast<std::uint32_t>(scripted(0x4a89b3f2, 0x2a96ed21)) != kDialogOk) return false;
    }
    if (!SL::spMS2) return true;
    cRZAutoRefCount<cRZMessage2Standard> message(new cRZMessage2Standard());
    message->SetType(kMessageQuit);
    SL::spMS2->GeneralMessagePostToTarget(message.get(), static_cast<cIGZMessageTarget2*>(this));
    return true;
}

bool cSC4App::RequestQuitFromRegion(bool askPlayer) {
    if (askPlayer) {
        if (sMacQuitDialogIsShowing_) return false;
        sMacQuitDialogIsShowing_ = true;
        std::uint32_t answer;
        {
            sc4::cSC4AutoGamePause pause(false, true, true, true);
            answer = static_cast<std::uint32_t>(cityHooks_.scriptedModal ? cityHooks_.scriptedModal(0x4a551b4c, 0xaa921f4f) : 0);
        }
        sMacQuitDialogIsShowing_ = false;
        if (answer == kDialogQuitRegionNo) return false;
    }
    if (SL::spMS2) {
        cRZAutoRefCount<cRZMessage2Standard> message(new cRZMessage2Standard());
        message->SetType(kMessageQuit);
        SL::spMS2->GeneralMessagePostToTarget(message.get(), static_cast<cIGZMessageTarget2*>(this));
    }
    return true;
}

bool cSC4App::RequestGoToRegionView(bool askPlayer) {
    if (askPlayer) {
        sc4::cSC4AutoGamePause pause(false, true, true, true);
        auto scripted = [&](std::uint32_t script, std::uint32_t window) { return static_cast<std::uint32_t>(cityHooks_.scriptedModal ? cityHooks_.scriptedModal(script, window) : 0); };
        if (!city_ || !city_->IsSaveDisabled()) {
            const std::uint32_t answer = scripted(0x6a553aa4, 0xaa921f4f);
            if (answer != kDialogNo) {
                if (answer != kDialogYes) return false;
                RunMessageServerPump(100, 500, 5000);
                RunMessageServer2Pump(100, 500, 5000);
                RequestSaveCity(false, cRZKeyboard::IsKeyDownNowStatic(kKeyMenu));
            }
        } else if (scripted(0x4a89b3f2, 0x2a96ed21) != kDialogOk) {
            return false;
        }
    }
    if (cityHooks_.flushGraphics) { cityHooks_.flushGraphics(); cityHooks_.flushGraphics(); }
    CloseCity();
    if (!city_) {
        if (cityHooks_.destroyCurrentScreen) cityHooks_.destroyCurrentScreen();
        if (cityHooks_.createRegionScreen) cityHooks_.createRegionScreen();
    }
    return true;
}

bool cSC4App::Quit() {
    const bool alreadyQuitting = field7c_;
    if (!alreadyQuitting) {
        field7c_ = true;
        if (cityHooks_.flushGraphics) { cityHooks_.flushGraphics(); cityHooks_.flushGraphics(); }
        CloseCity();
        if (cityHooks_.destroyCurrentScreen) cityHooks_.destroyCurrentScreen();
        if (cityHooks_.quitFramework) cityHooks_.quitFramework();
    }
    return !alreadyQuitting;
}

bool cSC4App::NewCity() {
    if (city_) return false;
    BusyScope busy(cityHooks_);
    sc4::cSC4AutoFlushInputMessageQueue flush(1000);
    if (cityHooks_.destroyCurrentScreen) cityHooks_.destroyCurrentScreen();
    if (cityHooks_.createCityScreen) cityHooks_.createCityScreen();
    city_ = cityHooks_.createCity ? cityHooks_.createCity() : nullptr;
    if (!city_) return false;
    if (regionalCity_ && regionalCity_->get()) regionalCity_->get()->SetupCity(city_);
    PostCityMessage(city_, 0x26d31ec0, 1, 0, false);
    city_->Init();
    PostCityMessage(city_, 0x2b96b3ea, 0, 0, true);
    PostCityMessage(city_, 0x26d31ec1, 1, 0, true);
    PostCityMessage(city_, 0xab96b05f, 0, 0, true);
    PostCityMessage(city_, 0xea8ae29a, 0, 0, true);
    AddRef();
    RunMessageServerPump(100, 500, 5000);
    RunMessageServer2Pump(100, 500, 5000);
    Release();
    if (SL::spRM) SL::spRM->ForceGarbageCollection();
    return true;
}

// Opens the city file as a database segment, registers it with the resource manager, reads the application record (which
// creates the city) and brings the city up; the "city loading" / "city loaded" messages carry the segment and the path.
bool cSC4App::LoadCity(const cRZString& path, cRZAutoRefCount<cISC4RegionalCity>* holder) {
    if (city_) CloseCity();
    regionalCity_ = holder;
    BusyScope busy(cityHooks_);
    sc4::cSC4AutoFlushInputMessageQueue flush(1000);
    bool result = false;
    cIGZPersistDBSegment* segment = cityHooks_.createCitySegment ? cityHooks_.createCitySegment() : nullptr;
    if (!segment) return false;
    if (segment->Init()) {
        segment->SetPath(path);
        if (segment->Open(true, false)) {
            const std::uint32_t version = cityHooks_.readLoadSaveVersion ? cityHooks_.readLoadSaveVersion(segment) : 0;
            if (version >= 0x3e && version <= 0x42) {
                cRZAutoRefCount<cRZString> pathCopy(new cRZString(path.ToChar()));
                {
                    cRZAutoRefCount<cRZMessage2Standard> loading(new cRZMessage2Standard());
                    loading->SetType(0x26c63340);
                    loading->SetVoid1(segment);
                    loading->SetVoid2(pathCopy.get());
                    SendMessage(*loading);
                }
                citySegment_ = segment;
                segment->AddRef();
                if (SL::spRM) SL::spRM->RegisterDBSegmentBack(*segment);
                if (cityHooks_.setSaveVersion && cityHooks_.setSaveVersion(segment, version) && cityHooks_.readAppRecord &&
                    cityHooks_.readAppRecord(segment, version, [&](cIGZIStream& stream) { return Read(stream, segment); })) {
                    city_->SetCitySerialNumber(regionalCity_->get()->GetCitySerialNumber());
                    city_->SetCitySaveFilePath(path);
                    city_->SetCityNameChanged(false);
                    if (cityHooks_.destroyCurrentScreen) cityHooks_.destroyCurrentScreen();
                    if (cityHooks_.createCityScreen) cityHooks_.createCityScreen();
                    cRZVariant versionValue(static_cast<std::uint32_t>(version));
                    {
                        cRZAutoRefCount<cRZMessage2Standard> loaded(new cRZMessage2Standard());
                        loaded->SetType(0x26c63341);
                        loaded->SetVoid1(segment);
                        loaded->SetVoid2(pathCopy.get());
                        loaded->SetVoid3(&versionValue);
                        SendMessage(*loaded);
                    }
                    {
                        cRZAutoRefCount<cRZMessage2Standard> finished(new cRZMessage2Standard());
                        finished->SetType(0x26c63342);
                        finished->SetVoid1(segment);
                        finished->SetVoid2(pathCopy.get());
                        SendMessage(*finished);
                    }
                    PostCityMessage(city_, 0x26d31ec0, 0, 0, false);
                    city_->Init();
                    PostCityMessage(city_, 0x2b96b3ea, 0, 0, true);
                    PostCityMessage(city_, 0x26d31ec1, 0, 0, true);
                    PostCityMessage(city_, 0xab96b05f, 0, 0, true);
                    PostCityMessage(city_, 0xea8ae29a, 0, 0, true);
                    result = true;
                }
                if (!result && city_) {
                    city_->Release();
                    city_ = nullptr;
                }
            }
        }
    }
    if (!result) UnregisterCityDBSegment();
    segment->Release();
    AddRef();
    RunMessageServerPump(100, 500, 5000);
    RunMessageServer2Pump(100, 500, 5000);
    Release();
    if (SL::spRM) SL::spRM->ForceGarbageCollection();
    return result;
}

void cSC4App::UnregisterCityDBSegment() {
    if (!citySegment_) return;
    if (SL::spRM) SL::spRM->UnregisterDBSegment(*citySegment_);
    citySegment_->Close();
    citySegment_->Shutdown();
    citySegment_->Release();
    citySegment_ = nullptr;
}

bool cSC4App::PartialCloseCity() {
    bool result = false;
    if (city_) {
        sc4::SC4GamePause(true, false, true, true, true);
        RunMessageServerPump(100, 500, 5000);
        RunMessageServer2Pump(100, 500, 5000);
        PostCityMessage(city_, 0x26d31ec2, 0, 0, false);
        city_->Shutdown();
        PostCityMessage(city_, 0x26d31ec3, 0, 0, true);
        city_->Release();
        city_ = nullptr;
        UnregisterCityDBSegment();
        result = true;
    }
    if (SL::spRM) SL::spRM->ForceGarbageCollection();
    return result;
}

// The region screen's refresh for the closed city (which city of the region is selected, which neighbours need a new
// thumbnail) is the host's regionCityClosed hook until the region classes are recovered.
bool cSC4App::CloseCity() {
    cISC4RegionalCity* regional = regionalCity_ ? regionalCity_->get() : nullptr;
    std::string savePath;
    if (regional) {
        cRZString path;
        regional->GetCitySaveFilePath(path);
        savePath = path.ToChar();
    }
    if (!PartialCloseCity()) return false;
    if (regional && cityHooks_.regionCityClosed) cityHooks_.regionCityClosed(regional, savePath);
    if (SL::spRM) SL::spRM->ForceGarbageCollection();
    return true;
}
