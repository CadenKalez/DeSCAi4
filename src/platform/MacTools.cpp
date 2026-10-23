#include "platform/MacTools.h"

#include <time.h>

namespace MacTools {
namespace {

IMacToolsHost* gHost = nullptr;

// The registered SDL user event type for PostPrivateEvent; 0 until the first call.
std::uint32_t gEventType = 0;

}  // namespace

IMacToolsHost* Host() { return gHost; }
void SetHost(IMacToolsHost* host) { gHost = host; }

// MacTools::FindCDROM(char const*) @ 0x100796c12 — the Mac port never looks for a CD, it answers null.
char const* FindCDROM(char const* cdName) {
    (void)cdName;
    return nullptr;
}

// MacTools::Sleep(unsigned int) @ 0x100796c1a
void Sleep(std::uint32_t milliseconds) {
    timespec ts{};
    if (milliseconds == 0) {
        ts.tv_nsec = 1;     // a zero sleep still yields
    } else {
        ts.tv_sec = milliseconds / 1000;
        ts.tv_nsec = static_cast<long>(milliseconds % 1000) * 1'000'000L;
    }
    nanosleep(&ts, nullptr);
}

// MacTools::PostPrivateEvent(unsigned long, unsigned long, unsigned long) @ 0x100796ca6
bool PostPrivateEvent(std::uint64_t message, std::uint64_t paramA, std::uint64_t paramB) {
    IMacToolsHost* host = Host();
    if (!host) return false;
    if (gEventType == 0) gEventType = static_cast<std::uint32_t>(host->RegisterEvents(1));
    SdlEvent event{};
    event.type = gEventType;
    event.message = static_cast<std::uint32_t>(message);   // the original stores only the low half at +0x0c
    event.paramA = paramA;
    event.paramB = paramB;
    return host->PushEvent(event) != 0;
}

// MacTools::PostWindowsEvent(unsigned long, unsigned long, unsigned long) @ 0x100796c6d
// Drops the window messages the port translates itself and forwards everything else. The original
// works in unsigned 64-bit arithmetic, so an id below 0x200 wraps in `message - 0x200` and is
// forwarded; that is reproduced here rather than "fixed".
bool PostWindowsEvent(std::uint64_t message, std::uint64_t paramA, std::uint64_t paramB) {
    const std::uint64_t command = message - 0x200;                       // wraps below 0x200
    const bool isTranslatedCommand =
        command <= 10 && ((0x437ULL >> (command & 0x3f)) & 1) != 0;      // 0x200,01,02,04,05,0a
    const std::uint64_t notification = message - 0x100;                  // wraps below 0x100
    const bool isTranslatedNotification = notification <= 1;             // 0x100, 0x101
    const bool isSystemCommand = message == 0x1c;                        // WM_ACTIVATEAPP
    if (isTranslatedCommand || isTranslatedNotification || isSystemCommand) return false;
    return PostPrivateEvent(message, paramA, paramB);
}

// MacTools::MessageBoxA(__CFString const*, unsigned char) @ 0x100796d39
void MessageBoxA(const void* cfString, unsigned char errorFlag) {
    IMacToolsHost* host = Host();
    if (!host || !cfString) return;
    if (!host->IsMainThread()) return;     // off the main thread the original shows nothing at all
    char text[kMessageBoxTextSize] = {};
    if (!host->CFStringToUTF8(cfString, text, static_cast<int>(kMessageBoxTextSize))) return;
    const std::uint32_t flags = errorFlag == 0 ? kMessageBoxInformation : kMessageBoxError;
    host->ShowMessageBox(flags, "", text, host->MainWindow());
}

// MacTools::MessageBoxA(char const*, unsigned char) @ 0x100796dce
// Quirk kept: the flag byte is not forwarded - the original always passes 0, so the box is an
// information box whatever the caller asked for.
void MessageBoxA(char const* text, unsigned char errorFlag) {
    (void)errorFlag;
    IMacToolsHost* host = Host();
    if (!host || !text) return;
    void* cfString = host->CreateCFString(text);
    if (!cfString) return;
    MessageBoxA(cfString, 0);
    host->ReleaseCFString(cfString);
}

}  // namespace MacTools
