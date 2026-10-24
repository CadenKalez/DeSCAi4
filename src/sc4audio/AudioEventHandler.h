// cSC4AudioEventHandler — the game's audio message dispatcher, and cSC4AudioEventHandlerDisasterFire, the
// fire-occupant filter that feeds the disaster sound layer. Both classes live in the original's
// SC4AudioEventHandler.cpp (the only module initialiser in the range, __GLOBAL__sub_I_SC4AudioEventHandler.cpp
// @ 0x1003efe16, belongs to it).
//
// cSC4AudioEventHandler is the class that actually starts the region music: it owns the framework's cIGZRadio
// (service 0x1d07b39f / iid 0xe0510260, recovered in rz/Radio.{h,cpp}), the sound system (0x23cecba0 / 0x106077d2),
// the soundscape manager (0x49e50ba1 / 0x29e39db4, sc4audio/AudioScapeManager.{h,cpp}) and a stack of modal windows.
// It listens on four interfaces of its own:
//   +0x00 cIGZSndEventHandler  DoEvent(cIGZSndEventStd*)       (registered with the sound system)
//   +0x08 cIGZMessageTarget    DoMessage(cGZMessage&)          (SL::spMS, the old server, modal window types)
//   +0x10 cIGZMessageTarget2   DoMessage(cIGZMessage2*)        (SL::spMS2, every game message below)
//   +0x18 cIGZInitShutdown     Init()/Shutdown()
// All four views share the one reference count at +0x3c; QueryInterface answers 1 / 0xbb523bb2 with the primary
// view, 0x452294aa with +0x10 and 0x89e51472 with +0x18, and nothing with +0x08 (quirk kept).
//
// The music path (traced from cSC4WinRegionScreen::Init @ 0x100237d58, verified at 0x1003ee462/0x1003ef5ae):
//   * the region screen posts a cRZMessage2Standard of type 0x2a3ad653 with Data1 = 1 to SL::spMS2 (mayor mode
//     posts Data1 = 0; the city-init handler below posts 0 itself once an established city is loaded);
//   * HandleRadioMessages checks the type, takes `mode = Data1` and `fade = cSC4AudioPreferencesWinProc::kFadeOutTimeMs`
//     (0x100b07248 = 4000) overridden by Data2 when GetHasData2(), then:
//         mode == 1 -> _SetRadioStation("krgn", fade, true)      (GameData/Radio/Stations/Region/krgn.id)
//         mode == 0 -> _SetRadioStation("kmay", fade, true)      (the mayor screen station)
//   * _SetRadioStation quits the on-demand state, early-outs when the station is already current, and otherwise
//     calls cIGZRadio::SetStation(callSign, &FadeOptions{targetVolume = 0, durationMs = fade}) - vslot 0x70,
//     recovered in rz/Radio.cpp:345. With fade == false it passes a null FadeOptions instead.
//
// Messages registered by Init (13), in registration order: 0x26d31ec1 city init, 0x26d31ec2 city shutdown,
// 0xc53d10aa tuning reload (handled, no action), 0x26d31ec4 post mayor music, 0x26d31ec5 post region music,
// 0x4a7fb7e9 listener sound property, 0x2a3ad653 radio station, 0xa611b34 radio station playlist options,
// 0x2a611b51 stop playback, 0xaa611b6e play song, 0xaa650860 radio on/off + volume, 0xaa650861 on-demand state,
// 0x4348b111 focus change. Shutdown removes the same 13 (in a different order) and also the two old-server
// modal-window types 0x62ba7b42/0x62ba7b43.
//
// DoMessage routes: 0x2a3ad653 -> HandleRadioMessages; 0xfa0c6390 (mouse messages while a modal window is up) ->
// HandleMouseMessagesForModalWindow; 0x26d31ec1/0x26d31ec2 -> HandleCityInitShutdownMessages; 0x89f10866 /
// 0xa9f10e45 / 0x49e7b1e2 / 0xc9e7b1e8 -> HandleConstructionMessages; 0x894671f6 / 0xe94671eb / 0xc9f1011a /
// 0x49f10111 -> HandleDispatchManagerMessages; 0x8a385dd8 / 0xa9e3b6e3 / 0xaa396960 / 0xea3ada33 -> and
// 0x49e64801 -> HandleAutomataMessages. The remaining ids are handled inline (see DoMessage in the .cpp).
//
// cSC4AudioEventHandlerDisasterFire (object 0x50 bytes) is the occupant filter the disaster fire layer registers:
// bases cIGZMessageTarget2 (+0), cIGZInitShutdown (+8), cSC4OccupantFilter (+0x10, its cIGZSerializable view at
// +0x18) and cIGZSndEventHandler (+0x28). Create() returns the +8 view. It subscribes to occupant inserted/removed
// (0x99ef1142/0x99ef1143) and city init (0x26d31ec1), and mirrors every fire occupant (iid 0x69d25c60, recovered in
// sc4/FireOccupant.h) into soundscape layer 0x49e4ce85, named "Fires".
//
// Quirks kept (each commented at the place it shows):
//  * Init does a second sound-system lookup instead of using the ctor's +0x28 member, and does not assign it back.
//  * QueryInterface answers iid 1 with the +0x18 view in the disaster class, and never answers the +0x08
//    cIGZMessageTarget view in either class.
//  * Release destroys at a count of <= 1 through the deleting destructor without decrementing first.
//  * The modal-window list is cleared in the destructor without releasing the windows it holds.
//  * DoEvent returns true only when the sound system is missing; every handled event answers false.
//  * HandleTorchControlMessages null-checks RZ::SS() but posts through the +0x28 member.
//  * The construction handlers key the sound instances by the low 32 bits of the occupant pointer.
//  * HandleConstructionMessages re-checks the soundscape pointer it already required at entry.
//  * HandleMouseMessagesForModalWindow reads the back of an empty list (the sentinel's size field, i.e. null).
//  * The random retrigger delay multiplies rand() by 3000 in 32-bit arithmetic (wraps; reproduced on purpose).
//  * AddRef/Release of the disaster class use its own +0x34 count; the cSC4OccupantFilter base's count is unused.
//
// Deviations forced by safety (each commented at the place it shows):
//  * The modal mouse handler skips a null back window (the original dereferences it and crashes on an empty list)
//    and skips a null mouse payload.
//  * The message posts guard SL::spMS2 / RZ::SS() / the preference blob where the original dereferences them
//    unchecked.
//  * _SetRadioStation initialises the GetCurrentStation out parameter to 0; the original reads it uninitialised
//    when the station list is empty.
//  * QueryInterface keeps the original's unchecked out pointer (a null out pointer is a caller bug).
//  * The occupant-manager callback @ 0x1003ece2a is a free function in the original and a static member here.
#pragma once
#include <cstddef>
#include <cstdint>
#include <list>

#include "audio/interfaces/cIGZSndEventStd.h"
#include "audio/interfaces/cIGZSndSys.h"
#include "gz/cGZMessageServer.h"
#include "gz/cIGZInitShutdown.h"
#include "gz/cIGZMessage2.h"
#include "rz/Radio.h"
#include "rz/cRZString.h"
#include "rz/cRZTimer.h"
#include "rz/cRZSysServPtr.h"
#include "sc4/OccupantManager.h"   // cSC4OccupantFilter (the disaster variant's base)

class cIGZFileSystem;
class cIGZWin;
class cISC4AudioScapeManager;

namespace sc4 {
class cISC4Occupant;
class cISC4OccupantFilter;
}  // namespace sc4

namespace sc4audio {

// ---- interface / service ids ---------------------------------------------------------------
constexpr std::uint32_t kGZIID_cSC4AudioEventHandler = 0xbb523bb2;   // the primary view (also cIGZSndEventHandler)
constexpr std::uint32_t kGZIID_cISC4AutomatonAudio = 0xa9b40f05;      // cISC4Automaton, for GetPosition
constexpr std::uint32_t kGZIID_cISC4NetworkOccupantAudio = 0xa821ef94;   // cISC4NetworkOccupant
constexpr std::uint32_t kGZIID_cISC4OccupantAudio = 0x26bb767e;       // cISC4Occupant
constexpr std::uint32_t kGZIID_cISC4FireOccupantAudio = 0x69d25c60;   // cISC4FireOccupant (sc4/FireOccupant.h)
constexpr std::uint32_t kGZIID_cIGZMouseMessageAudio = 0x1a0c615f;    // gz::cIGZMouseMessage
constexpr std::uint32_t kGZIID_cIGZGraphicsMessageServer = 0x3ac7a210;   // what RZ::GS() 0x78 answers

// ---- message types (see the header comment for the registration order) ----------------------
constexpr std::uint32_t kMessageCityInit = 0x26d31ec1;
constexpr std::uint32_t kMessageCityShutdown = 0x26d31ec2;
constexpr std::uint32_t kMessageTuningReload = 0xc53d10aa;
constexpr std::uint32_t kMessagePostMayorMusic = 0x26d31ec4;       // re-posts kMessageRadioStation Data1 = 0
constexpr std::uint32_t kMessagePostRegionMusic = 0x26d31ec5;      // re-posts kMessageRadioStation Data1 = 1
constexpr std::uint32_t kMessageListenerProperty = 0x4a7fb7e9;
constexpr std::uint32_t kMessageRadioStation = 0x2a3ad653;         // Data1 1 = region "krgn", 0 = mayor "kmay"
constexpr std::uint32_t kMessageRadioStationOptions = 0xa611b34;   // Data1 mode, Data2 exclusion list, Data3 playlist mode
constexpr std::uint32_t kMessageStopPlayback = 0x2a611b51;
constexpr std::uint32_t kMessagePlaySong = 0xaa611b6e;             // Data1 (void*) = cRZString song
constexpr std::uint32_t kMessageRadioOnOffVolume = 0xaa650860;
constexpr std::uint32_t kMessageRadioOnDemand = 0xaa650861;
constexpr std::uint32_t kMessageFocusChange = 0x4348b111;

// The two stations the region and mayor screens ask the radio to switch to (the strings at 0x1009f4498 / 0x1009f4493).
constexpr const char* kRegionStationCallSign = "krgn";
constexpr const char* kMayorStationCallSign = "kmay";

constexpr std::uint32_t kMessageModalWindowOpened = 0x62ba7b42;    // old message server, Data2 = cIGZWin*
constexpr std::uint32_t kMessageModalWindowClosed = 0x62ba7b43;

// ---- sound events (cIGZSndEventHandler) -----------------------------------------------------
constexpr std::uint32_t kEventListenerTuningChanged = 0xaa36d9b6;  // -> sound system EnumerateTargets
constexpr std::uint32_t kEventSoundRegisterValue = 0x0baab7a4;     // Data1 -> global property 0x7f
constexpr std::uint32_t kEventTorchStop = 0x8a10611a;
constexpr std::uint32_t kEventTorchMouseDown = 0x8a10611d;
constexpr std::uint32_t kEventTorchFlare = 0xaa96fe8f;             // Data1 != 0 = flare on
constexpr std::uint32_t kEventTorchMouseUp = 0xa106122;
constexpr std::uint32_t kEventTorchStopLoop = 0x2a106113;
constexpr std::uint32_t kEventTorchMove = 0x4a106126;

// ---- automation / construction / dispatch message types -------------------------------------
constexpr std::uint32_t kMessageAutomataEffect = 0x8a385dd8;       // one-shot positional sound
constexpr std::uint32_t kMessageAutomataInstance = 0xa9e3b6e3;     // add / remove a soundscape instance
constexpr std::uint32_t kMessageAutomataStop = 0xaa396960;          // stop by instance id
constexpr std::uint32_t kMessageAutomataCreate = 0xea3ada33;        // create a soundscape instance
constexpr std::uint32_t kMessageAutomataOther = 0x49e64801;
constexpr std::uint32_t kMessageConstructionNetwork = 0x89f10866;
constexpr std::uint32_t kMessageConstructionUpdate = 0xa9f10e45;
constexpr std::uint32_t kMessageConstructionOccupantInsert = 0x49e7b1e2;
constexpr std::uint32_t kMessageConstructionOccupantToggle = 0xc9e7b1e8;
constexpr std::uint32_t kMessageDispatchFire = 0x894671f6;
constexpr std::uint32_t kMessageDispatchPolice = 0xe94671eb;
constexpr std::uint32_t kMessageDispatchPylonFire = 0xc9f1011a;
constexpr std::uint32_t kMessageDispatchPylonPolice = 0x49f10111;

// ---- sound / layer ids ----------------------------------------------------------------------
constexpr std::uint32_t kSoundInstanceEventStart = 0xea451903;     // cSC4AudioSoundManager StartSound
constexpr std::uint32_t kSoundInstanceEventStop = 0x6a451908;
constexpr std::uint32_t kSoundInstanceEventPosition = 0xca45190f;
constexpr std::uint32_t kSoundInstanceEventVolume = 0x4a451915;
constexpr std::uint32_t kSoundIdTorchOneShot = 0xa9f14c47;         // throttled by the cRZTimer
constexpr std::uint32_t kSoundIdTorchLoopA = 0x6a10abcf;
constexpr std::uint32_t kSoundIdTorchLoopB = 0xa10ad41;
constexpr std::uint32_t kSoundIdTorchResponse = 0x2aa73c50;
constexpr std::uint32_t kSoundIdTorchStop = 0x6a10ac51;

constexpr std::uint32_t kLayerAutomataDefault = 0x69f01280;
constexpr std::uint32_t kLayerAutomata1 = 0x4a5e7ffc;               // Data2 == 1, Data1 == 0
constexpr std::uint32_t kLayerAutomata1b = 0x0a5e802c;              // Data2 == 1, Data1 != 0
constexpr std::uint32_t kLayerAutomata2 = 0x2a5e7f2c;               // Data2 == 2, Data1 == 0
constexpr std::uint32_t kLayerAutomata2b = 0x0a5e7f51;              // Data2 == 2, Data1 != 0
constexpr std::uint32_t kLayerAutomata3 = 0x2a5e8067;               // Data2 == 3, Data1 == 0
constexpr std::uint32_t kLayerAutomata3b = 0x6a5e8088;              // Data2 == 3, Data1 != 0
constexpr std::uint32_t kLayerAutomataEnter = 0x49de8a94;           // instance added (Data1 > 0)
constexpr std::uint32_t kLayerAutomataLeave = 0xa9de8b59;           // instance removed (Data1 < 0)
constexpr std::uint32_t kLayerConstructionCrew = 0xc9dc0a61;        // construction occupant insert
constexpr std::uint32_t kLayerConstructionOccupancy = 0x49e7b4b0;   // construction occupant add/remove
constexpr std::uint32_t kLayerConstructionNetworkA = 0xaa62c82a;    // construction network sounds
constexpr std::uint32_t kLayerConstructionNetworkB = 0xea62c800;
constexpr std::uint32_t kDispatchSoundFire = 0x09c66951;
constexpr std::uint32_t kDispatchSoundPolice = 0x29d99e9e;

// The soundscape layer of the disaster fire handler and its three registered sounds (min/max instance window).
constexpr std::uint32_t kLayerDisasterFire = 0x49e4ce85;
constexpr const char* kDisasterFireLayerName = "Fires";
constexpr std::uint32_t kDisasterFireSoundSmall = 0x29dc19ef;
constexpr std::uint32_t kDisasterFireSoundMedium = 0x09dc19a2;
constexpr std::uint32_t kDisasterFireSoundLarge = 0x09dc1982;

// The prefix Init appends to the file system's data directory (the original's static cRZString kRadioBaseDir
// @ 0x100b8a230, built as "Radio" + '/' by the module initialiser of this translation unit).
constexpr const char* kRadioBaseDirectory = "Radio/";
// cSC4AudioPreferencesWinProc::kFadeOutTimeMs @ 0x100b07248.
constexpr std::uint32_t kDefaultFadeOutTimeMs = 4000;
// The default displacement radius the automata insert with (float 50.0f @ 0x100a16e84).
constexpr float kAutomataInstanceRadius = 50.0f;
// Offsets inside the 0x1734-byte preference blob (sc4/AppPreferences.h; the audio block starts at +4).
constexpr std::size_t kPrefsEffectsVolume = 0x008;     // u32, 0..1024
constexpr std::size_t kPrefsMusicVolume = 0x00c;       // u32, 0..1024
constexpr std::size_t kPrefsMusicEnabled = 0x010;      // u32, non-zero = music on
constexpr std::size_t kPrefsAudioFlags = 0x028;        // u8, bit 0 = music options configured
constexpr std::size_t kPrefsAudioResetFlag = 0x06a;    // u8, 1 = apply the defaults on the next Init
// The two slider defaults Init converts (GetVolumeFromSliderValue(0x2d0, music) / (0x400, effects)).
constexpr std::uint32_t kMusicSliderDefault = 0x2d0;
constexpr std::uint32_t kEffectsSliderDefault = 0x400;

// cISC4Automaton (iid 0xa9b40f05) as this handler uses it: only [7] GetPosition is called, but the two
// AsOccupant slots in front of it have to be declared to keep the offset. cSC4Automaton vtable 0x100a73b30.
class cISC4AutomatonAudio : public cIGZUnknown {
public:
    virtual bool Init() = 0;                         // [3] +0x18
    virtual bool Shutdown() = 0;                     // [4] +0x20
    virtual sc4::cISC4Occupant* AsOccupant() const = 0;   // [5] +0x28
    virtual sc4::cISC4Occupant* AsOccupant() = 0;         // [6] +0x30
    virtual void GetPosition(float* xyz) = 0;        // [7] +0x38
};

// What cSC4AudioEventHandler needs from neighbours that are outside src (see the AEH-* to-do rows): the original
// reaches them through cSC4AudioPreferencesWinProc's static service (SC4AudioPreferences.cpp), the graphics system
// and the occupant manager. The game integration installs the real adapter (docs/SEAMS.md); every method mirrors
// one call.
class IAudioEventHandlerHost {
public:
    virtual ~IAudioEventHandlerHost();

    // cSC4AudioPreferencesWinProc: the music preferences half of the audio options dialog.
    enum AudioType : std::uint32_t { kAudioTypeEffects = 0, kAudioTypeMusic = 1 };
    enum AudioMode : std::uint32_t { kAudioModeMayor = 0, kAudioModeRegion = 1 };
    virtual void PreferencesStaticInit() {}                                       // @ 0x1003a8d82
    virtual std::uint32_t FadeOutTimeMs() { return kDefaultFadeOutTimeMs; }       // kFadeOutTimeMs 0x100b07248
    // @ 0x1003abbf4: slider position (0..1024) -> volume (0..1024); the real curve lives in cSC4AudioPreferencesWinProc.
    virtual std::uint32_t VolumeFromSliderValue(std::uint32_t sliderValue, AudioType) { return sliderValue; }
    virtual void SetDefaultMusicListPreferences() {}                              // @ 0x1003ab27a
    virtual void UpdateExclusionList(AudioMode) {}                                // @ 0x1003ab450

    // SL::spSC4 vslot 0x130 (GetPreferences): the 0x1734-byte preference blob, or null.
    virtual std::uint8_t* PreferencesBlob();
    // RZ::GS() vslot 0x78 -> QueryInterface(0x3ac7a210): the server that delivers mouse messages while a modal
    // window is open. Borrowed; null when unavailable.
    virtual cIGZMessageServer2* MouseMessageServer() { return nullptr; }
    // SL::spCity vslot 0xb0 (cSC4City::GetEstablished): may the city-init handler start the mayor music?
    virtual bool CityEstablished() { return false; }

    // cISC4NetworkOccupant (iid 0xa821ef94) vslot 0x180 -> AsOccupant(), then the occupant's [8] GetPosition.
    // The network-occupant view is 0x180 bytes deep, so the two calls sit behind this seam.
    virtual bool NetworkOccupantPosition(cIGZUnknown* networkOccupant, float* xyz) { (void)networkOccupant; (void)xyz; return false; }

    // SL::spOccupantManager vslot 0x168 (IterateOccupants) with a null cell rectangle: every occupant of the
    // city that the filter accepts, until the callback returns false.
    using OccupantCallback = bool (*)(sc4::cISC4Occupant* occupant, void* context);
    virtual bool IterateOccupants(OccupantCallback callback, void* context, sc4::cISC4OccupantFilter* filter) {
        (void)callback; (void)context; (void)filter;
        return false;
    }
};

IAudioEventHandlerHost* AudioEventHandlerHost();                 // always non-null (built-in host)
void SetAudioEventHandlerHost(IAudioEventHandlerHost* host);

}  // namespace sc4audio

// ---------------------------------------------------------------------------------------------
// cSC4AudioEventHandler
// ---------------------------------------------------------------------------------------------
class cSC4AudioEventHandler final : public cIGZSndEventHandler,
                                    public cIGZMessageTarget,
                                    public cIGZMessageTarget2,
                                    public cIGZInitShutdown {
public:
    cSC4AudioEventHandler();                                   // @ 0x1003ed1a2
    ~cSC4AudioEventHandler();                                  // @ 0x1003ed2e0 / 0x1003ed3b0 / 0x1003ed3ba
    static cSC4AudioEventHandler* Create();                    // @ 0x1003dedb2 (new, ctor)

    // --- cIGZUnknown, shared by the four views: ids 1 / 0xbb523bb2 primary, 0x452294aa +0x10, 0x89e51472 +0x18
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1003efda6
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x1003efcf2
    std::uint32_t Release() override;                              // @ 0x1003efd2a

    // --- interface methods
    bool DoEvent(cIGZSndEventStd* event) override;                 // @ 0x1003ef652
    bool DoMessage(cGZMessage& message) override;                  // @ 0x1003edb82
    bool DoMessage(cIGZMessage2* message) override;                // @ 0x1003eddf0
    bool Init() override;                                          // @ 0x1003ed3e8
    bool Shutdown() override;                                      // @ 0x1003ed8ce

    // --- inspection (tests / integration)
    bool IsInitialized() const { return initialized_; }
    std::uint32_t refCount() const { return refCount_; }
    std::size_t modalWindowCount() const { return modalWindows_.size(); }
    cIGZWin* lastModalWindow() const;
    cIGZRadio* radio() const { return radio_; }
    cIGZSndSys* soundSystem() const { return soundSystem_; }
    cISC4AudioScapeManager* scapeManager() const { return scapeManager_; }
    std::uint32_t retriggerDelayMs() const { return retriggerDelayMs_; }   // +0x78
    std::int32_t viewportWidth() const { return viewportWidth_; }          // +0x40
    std::int32_t viewportHeight() const { return viewportHeight_; }        // +0x44

    // --- the paths, public so the test can drive them
    bool HandleModalMessages(cGZMessage& message);                  // @ 0x1003edb9c
    bool HandleMouseMessagesForModalWindow(cIGZMessage2* message);   // @ 0x1003ee2f6
    bool HandleRadioMessages(cIGZMessage2* message);                // @ 0x1003ee462
    void _SetRadioStation(const char* callSign, std::uint32_t fadeMs, bool fade);   // @ 0x1003ef5ae
    bool HandleConstructionMessages(cIGZMessage2* message);         // @ 0x1003ee568
    bool HandleCityInitShutdownMessages(cIGZMessage2* message);     // @ 0x1003eea44
    bool HandleAutomataMessages(cIGZMessage2* message);             // @ 0x1003eedda
    bool HandleDispatchManagerMessages(cIGZMessage2* message);      // @ 0x1003ef31e
    bool HandleTorchControlMessages(cIGZSndEventStd* event);        // @ 0x1003ef790

private:
    // The ctor's two published framework services; their default constructors perform the lookup the original's
    // constructor does (RZGetFramework()->GetSystemService).
    cRZSysServPtr<cIGZRadio, kGZSID_cRZRadio, kGZIID_cIGZRadio> radio_;              // +0x20
    cRZSysServPtr<cIGZSndSys, kGZSID_cGZSndSys, kGZIID_cIGZSndSys> soundSystem_;     // +0x28
    cISC4AudioScapeManager* scapeManager_ = nullptr;   // +0x30 owned (manual AddRef/Release, see Init/Shutdown)
    bool initialized_ = false;                         // +0x38
    std::uint32_t refCount_ = 0;                       // +0x3c
    std::int32_t viewportWidth_ = 0;                   // +0x40 (city init copies SC4ViewUtilities +0x30)
    std::int32_t viewportHeight_ = 0;                  // +0x44 (and +0x34); nothing else reads them
    std::list<cIGZWin*> modalWindows_;                 // +0x48 (holds one reference per window)
    cRZTimer timer_{cRZTimer::kMilliseconds};          // +0x60, constructed with units 4
    std::uint32_t retriggerDelayMs_ = 0;               // +0x78 random delay of the torch one-shot sound
};

// ---------------------------------------------------------------------------------------------
// cSC4AudioEventHandlerDisasterFire
// ---------------------------------------------------------------------------------------------
class cSC4AudioEventHandlerDisasterFire final : public cIGZMessageTarget2,
                                               public cIGZInitShutdown,
                                               public sc4::cSC4OccupantFilter,
                                               public cIGZSndEventHandler {
public:
    cSC4AudioEventHandlerDisasterFire();                       // @ 0x1003ec79c (0x50-byte object)
    ~cSC4AudioEventHandlerDisasterFire();                      // @ 0x1003ec83a
    // @ 0x1003dedea: new 0x50, construct, return the cIGZInitShutdown view (+8).
    static cIGZInitShutdown* Create();

    // --- cIGZUnknown: 1 / 0x89e51472 -> +8, 0xbb523bb2 -> +0x28, 0x452294aa -> +0
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1003ed02c
    std::uint32_t AddRef() override { return ++refCount_; }        // @ 0x1003ecf4a (its own +0x34 count)
    std::uint32_t Release() override;                              // @ 0x1003ecf90

    // --- interfaces
    bool DoMessage(cIGZMessage2* message) override;                // @ 0x1003ecc60
    bool Init() override;                                          // @ 0x1003ec91e
    bool Shutdown() override;                                      // @ 0x1003ecb84
    bool DoEvent(cIGZSndEventStd* event) override { (void)event; return false; }   // @ 0x1003ed0a8 (empty)
    bool IsOccupantIncluded(sc4::cISC4Occupant* occupant) override;   // @ 0x1003ece84

    // @ 0x1003ecef0: position the occupant into the disaster layer.
    void IterateFireOccupantCallback(sc4::cISC4Occupant* occupant);
    // @ 0x1003ece2a (free function in the original): the occupant-manager callback used at city init.
    static bool IterateFireOccupantsOnInitCallback(sc4::cISC4Occupant* occupant, void* context);

    bool IsInitialized() const { return initialized_; }
    std::uint32_t refCount() const { return refCount_; }
    cISC4AudioScapeManager* scapeManager() const { return scapeManager_; }

private:
    bool initialized_ = false;                     // +0x30
    std::uint32_t refCount_ = 0;                   // +0x34
    cISC4AudioScapeManager* scapeManager_ = nullptr;   // +0x38 owned (manual AddRef/Release)
};
