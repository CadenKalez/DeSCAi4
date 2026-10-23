// MacTools — the small Aspyr utility namespace the Windows-shaped game code calls (6 functions,
// 0x100796c12..0x100796e0d): sleeps, the CD-ROM probe (always fails on the port), the two event
// posters and the two message boxes. Everything it touches outside the game is SDL or CoreFoundation,
// so those calls go through IMacToolsHost instead of being linked directly.
//
// Original behaviour kept:
//  - Sleep(0) still yields: it sleeps 1 ns rather than returning immediately (0x100796c1a).
//  - FindCDROM answers 0 unconditionally (0x100796c12).
//  - PostWindowsEvent drops exactly the Windows command/notification messages the port handles
//    elsewhere: ids 0x200, 0x201, 0x202, 0x204, 0x205 and 0x20a (the bits set in 0x437), ids
//    0x100 and 0x101, and 0x1c. Everything else is forwarded to PostPrivateEvent (0x100796c6d).
//  - PostPrivateEvent registers one SDL user event the first time and pushes a 0x38-byte event with
//    the message id at +0x0c and the two parameters at +0x18 / +0x20 (0x100796ca6).
//  - MessageBoxA(CFStringRef) only shows a box on the main thread; the box flags are 0x10 unless the
//    flag byte is zero, then 0x40 (0x100796d39).
//  - Quirk kept: MessageBoxA(char const*) always passes 0 for that flag byte, so the caller's flag is
//    ignored and the box comes out as an information box (0x100796dce).
#pragma once
#include <cstddef>
#include <cstdint>

namespace MacTools {

// One SDL_Event, as the original fills it in: type at +0x00, then zeros up to 0x38.
struct SdlEvent {
    std::uint32_t type = 0;                 // +0x00: registered user event type
    std::uint32_t pad0[2] = {};             // +0x04
    std::uint32_t message = 0;              // +0x0c: low half of the message id
    std::uint32_t pad1[2] = {};             // +0x10
    std::uint64_t paramA = 0;               // +0x18
    std::uint64_t paramB = 0;               // +0x20
    std::uint8_t rest[0x38 - 0x28] = {};    // +0x28: SDL_Event is 0x38 bytes
};

class IMacToolsHost {
public:
    virtual ~IMacToolsHost() = default;
    virtual int RegisterEvents(int count) = 0;                                    // SDL_RegisterEvents
    virtual int PushEvent(const SdlEvent& event) = 0;                             // SDL_PushEvent
    virtual bool IsMainThread() = 0;                                              // pthread_main_np() != 0
    virtual void* MainWindow() = 0;                                               // MacDisplay::GetMainWindow
    virtual void ShowMessageBox(std::uint32_t flags, const char* title, const char* text, void* window) = 0;   // SDL_ShowSimpleMessageBox
    virtual void* CreateCFString(const char* utf8) = 0;                           // CFStringCreateWithCString
    virtual bool CFStringToUTF8(const void* cfString, char* out, int size) = 0;   // CFStringGetCString
    virtual void ReleaseCFString(void* cfString) = 0;                             // CFRelease
};

IMacToolsHost* Host();
void SetHost(IMacToolsHost* host);

constexpr std::uint32_t kMessageBoxError = 0x10;          // SDL_MESSAGEBOX_ERROR
constexpr std::uint32_t kMessageBoxInformation = 0x40;    // SDL_MESSAGEBOX_INFORMATION
constexpr std::size_t kMessageBoxTextSize = 0x200;        // the original's 512-byte buffer

char const* FindCDROM(char const* cdName);                    // @ 0x100796c12: always null
void Sleep(std::uint32_t milliseconds);                       // @ 0x100796c1a

// @ 0x100796c6d: true when the event was pushed.
bool PostWindowsEvent(std::uint64_t message, std::uint64_t paramA, std::uint64_t paramB);
// @ 0x100796ca6: true when the event was pushed.
bool PostPrivateEvent(std::uint64_t message, std::uint64_t paramA, std::uint64_t paramB);

void MessageBoxA(const void* cfString, unsigned char errorFlag);   // @ 0x100796d39
void MessageBoxA(char const* text, unsigned char errorFlag);       // @ 0x100796dce

}  // namespace MacTools
