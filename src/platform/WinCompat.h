// The Windows-compatibility layer Aspyr linked in so the PC code could keep calling Win32 / Winsock entry points
// (13 functions at 0x10064214c..0x100642456). Only what the recovered game code reaches is here: the interlocked counters,
// fcvt, setenv (a stub), the keyboard / mouse state queries (answered from SDL), the window rectangle queries (from the
// display), SetCursorPos, and the Winsock calls the socket classes use (closesocket, ioctlsocket, WSAGetLastError, which
// reports BSD errno values 1..0x57 shifted by 10000 so they land on the WSAE* numbers).
// Seams: IWinCompatHost stands for SDL_GetMouseState / SDL_GetModState, MacDisplay::GetClientRect, CGWarpMouseCursorPosition
// and MacDebug::MessageToAlert.
#pragma once
#include <cstdint>

namespace wincompat {

struct RECT { std::int32_t left, top, right, bottom; };

class IWinCompatHost {
public:
    virtual ~IWinCompatHost() = default;
    virtual std::uint32_t MouseButtons() = 0;                     // SDL_GetMouseState(0, 0): bit 0 left, 1 middle, 2 right
    virtual std::uint32_t ModState() = 0;                         // SDL_GetModState
    virtual void ClientRect(void* window, RECT* out) = 0;         // MacDisplay::GetClientRect
    virtual void WarpCursor(double x, double y) = 0;              // CGWarpMouseCursorPosition
    virtual void Alert(const char* text) = 0;                     // MacDebug::MessageToAlert
};
IWinCompatHost* Host();
void SetHost(IWinCompatHost* host);

constexpr std::int32_t kWSABase = 10000;

std::int32_t InterlockedIncrement(volatile std::int32_t* value);   // @ 0x10064214c: the new value, with a barrier
std::int32_t InterlockedDecrement(volatile std::int32_t* value);   // @ 0x100642156
char* fcvt(double value, int digits, int* decimalPoint, int* sign);   // @ 0x100642162
int setenv(const char* name, const char* value, int overwrite);       // @ 0x1006421cd: does nothing, answers 1
int GetKeyState(int virtualKey);                                      // @ 0x10064225b
int GetAsyncKeyState(int virtualKey);                                 // @ 0x10064232c
void GetClientRect(void* window, RECT* out);                          // @ 0x1006423f3
void GetWindowRect(void* window, RECT* out);                          // @ 0x1006423fd (the same query as GetClientRect)
int ClientToScreen(void* window, void* point);                        // @ 0x100642407: leaves the point alone, answers 1
int SetCursorPos(short x, short y);                                   // @ 0x100642414
int closesocket(int socket);                                          // @ 0x100642440
int ioctlsocket(int socket, unsigned long request, void* argument);   // @ 0x10064244a
int WSAGetLastError();                                                // @ 0x100642456

}  // namespace wincompat
