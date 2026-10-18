// The BSD-socket wrappers of the networking layer: cRZSocketError (5 functions at 0x10079ce26..0x10079d656), the socket
// address records (0x10064eb1e, 0x10064eb2a), cRZNetSocket (60 functions + thunks at 0x10079b690..0x10079d64c) and
// cRZNetStreamSocket (19 functions at 0x10079c834..0x10079d69a). The sockets keep the Winsock vocabulary of the PC code:
// every failure stores the WSAGetLastError() number (errno + 10000 for the BSD range) and the cRZSocketError value it maps to.
// Layout of cRZNetSocket: +8 id, +0x10 socket (64-bit, -1 = none), +0x20 bound, +0x22 own address, +0x32 peer address,
// +0x44 domain / type / protocol (AF_INET, SOCK_STREAM, 0), +0x50 the error record (vptr, value at +0x58, platform error at
// +0x5c), +0x60 keep the socket on delete, +0x68 async notification target; cRZNetStreamSocket adds +0x80 the connect status
// (0 none, 2 connected, 3 listening, 4 shut down).
// Quirks kept: cRZINetSocketAddress stores the address family as a 16-bit 2 (a Windows sockaddr_in): the kernel replaces the
// length byte with the length it is given and bind ignores the family byte, so the layout still works; the send-delay option sets SO_DEBUG and the
// timeouts are 4-byte values where the kernel wants a timeval; the route option is inverted; IsSocketConnected polls with
// nfds = 1 and answers true for any open socket; a failed IsWaitingDataAvailable forgets the socket.
// Deviation: Accept initialises the address length (the original passes an uninitialised one).
#pragma once
#include <cstdint>

#include "gz/cGZMessageServer.h"
#include "rz/cRZString.h"

class cRZSocketAddress {
public:
    cRZSocketAddress() = default;   // @ 0x10064eb1e
    std::uint8_t bytes[16] = {};
};

// A Windows-layout sockaddr_in: family (16 bit), port, address.
class cRZINetSocketAddress : public cRZSocketAddress {
public:
    cRZINetSocketAddress(std::uint16_t port, std::uint32_t address);   // @ 0x10064eb2a
};

class cRZSocketError {
public:
    virtual ~cRZSocketError() = default;
    virtual void Init(int platformError);                       // @ 0x10079d1c4
    virtual int GetErrorValue() const { return value_; }        // @ 0x10079d656
    virtual void GetErrorString(cRZString& out) const;          // @ 0x10079d5f8
    const char* GetErrorStringCStr() const;                     // @ 0x10079d1e6
    static int GetErrorFromPlatformSpecificError(int platformError);   // @ 0x10079ce26

    int value_ = 1;           // +8
    int platformError_ = 0;   // +0xc
};

class cRZNetSocket {
public:
    enum ShutMode : int { kShutRead = 0, kShutWrite = 1, kShutBoth = 2 };
    enum WaitMode : std::uint32_t { kWaitRead = 1, kWaitWrite = 2 };
    static constexpr std::uint32_t kReadable = 1, kWritable = 2, kWaitFailed = 0xfffffffc;

    cRZNetSocket();
    virtual ~cRZNetSocket();

    virtual bool IsSocketCreated() const;                                        // @ 0x10079b982
    virtual std::int64_t GetSocket() const;                                      // @ 0x10079d62e
    virtual bool CloseSocket();                                                  // @ 0x10079b990
    virtual bool ShutDownSocket(int mode);                                       // @ 0x10079b9de
    virtual bool CreateSocket();                                                 // @ 0x10079b93e
    virtual bool BindSocket(const cRZSocketAddress& address);                    // @ 0x10079ba20
    virtual bool BindSocket();                                                   // @ 0x10079ba7c
    virtual bool SetSocketStyle(int domain, int type, int protocol);             // @ 0x10079b92c
    virtual bool SetSaveSocketOnDelete(bool keep);                               // @ 0x10079b898
    virtual bool GetSocketAddress(cRZSocketAddress& out, std::int64_t& socket);  // @ 0x10079ba8e
    virtual void GetMyAddress(cRZSocketAddress& out);                            // @ 0x10079bada
    virtual bool SetMyAddress(const cRZSocketAddress& address);                  // @ 0x10079bb4a
    virtual bool GetPeerAddress(cRZSocketAddress& out, std::int64_t& socket);    // @ 0x10079baec
    virtual void GetMyPeerAddress(cRZSocketAddress& out);                        // @ 0x10079bb38
    virtual bool SetPeerSocketAddress(const cRZSocketAddress& address);          // @ 0x10079bb62
    virtual cIGZMessageTarget* GetAsyncNotificationTarget();                     // @ 0x10079b8a4
    virtual bool SetAsyncNotificationTarget(cIGZMessageTarget* target);          // @ 0x10079b8ae
    virtual bool StartAsyncStandardNotification();                               // @ 0x10079b8e4
    virtual bool StartAsyncAcceptNotification();                                 // @ 0x10079b8fa
    virtual bool StartAsyncCustomNotification(int events);                       // @ 0x10079b910
    virtual bool StopAsyncNotification();                                        // @ 0x10079b918
    virtual void SetID(int id);                                                  // @ 0x10079d638
    virtual int GetID();                                                         // @ 0x10079d642
    virtual int GetLastError();                                                  // @ 0x10079d64c
    virtual bool GetWaitingDataSize(std::uint32_t& size, std::uint32_t timeoutMs);   // @ 0x10079bb7a
    virtual bool IsWaitingDataAvailable(std::uint32_t timeoutMs);                // @ 0x10079bc88
    virtual bool IsWritable(std::uint32_t timeoutMs);                            // @ 0x10079bd50
    virtual std::uint32_t IsReadableAndOrWritable(std::uint32_t waitMode, std::uint32_t timeoutMs);   // @ 0x10079be10
    virtual bool GetBroadcastOption(bool& on);                                   // @ 0x10079c394
    virtual bool GetDebugOption(bool& on);                                       // @ 0x10079c3f8
    virtual bool GetLingerOption(bool& on, std::uint32_t& seconds);              // @ 0x10079c45c
    virtual bool GetRouteOption(bool& on);                                       // @ 0x10079c4ce
    virtual bool GetKeepAliveOption(bool& on);                                   // @ 0x10079c532
    virtual bool GetOOBOption(bool& on);                                         // @ 0x10079c596
    virtual bool GetReceiveBufferOption(std::uint32_t& size);                    // @ 0x10079c5fa
    virtual bool GetSendBufferOption(std::uint32_t& size);                       // @ 0x10079c656
    virtual bool GetReuseAddressOption(bool& on);                                // @ 0x10079c6b2
    virtual bool GetSendDelayOption(bool& on);                                   // @ 0x10079c716
    virtual bool GetSendTimeout(std::uint32_t& ms);                              // @ 0x10079c77a
    virtual bool GetReceiveTimeout(std::uint32_t& ms);                           // @ 0x10079c7d6
    virtual bool SetBroadcastOption(bool on);                                    // @ 0x10079bf62
    virtual bool SetDebugOption(bool on);                                        // @ 0x10079bfbc
    virtual bool SetLingerOption(bool on, std::uint32_t seconds);                // @ 0x10079c016
    virtual bool SetRouteOption(bool on);                                        // @ 0x10079c076
    virtual bool SetKeepAliveOption(bool on);                                    // @ 0x10079c0d4
    virtual bool SetOOBOption(bool on);                                          // @ 0x10079c12e
    virtual bool SetReceiveBufferOption(std::uint32_t size);                     // @ 0x10079c188
    virtual bool SetSendBufferOption(std::uint32_t size);                        // @ 0x10079c1de
    virtual bool SetReuseAddressOption(bool on);                                 // @ 0x10079c234
    virtual bool SetSendDelayOption(bool on);                                    // @ 0x10079c28e
    virtual bool SetSendTimeout(std::uint32_t ms);                               // @ 0x10079c2e8
    virtual bool SetReceiveTimeout(std::uint32_t ms);                            // @ 0x10079c33e
    virtual bool Notification(std::uint32_t socket, int event, int error);       // @ 0x10079b78e
    virtual bool DoMessage(cGZMessage& message);                                 // @ 0x10079bf5a
    virtual bool SetupWindow();                                                  // @ 0x10079b886
    virtual bool ShutdownWindow();                                               // @ 0x10079b88e

    const cRZSocketError& Error() const { return error_; }

protected:
    void RecordError(int platformError);   // the pattern every failing call ends with
    bool Fail() { RecordError(WSAError()); return false; }
    static int WSAError();
    bool SetIntOption(int level, int option, int value);
    bool GetIntOption(int level, int option, int& value);

    int id_ = 0;                          // +8
    std::int64_t socket_ = -1;            // +0x10
    bool bound_ = false;                  // +0x20
    cRZSocketAddress myAddress_;          // +0x22
    cRZSocketAddress peerAddress_;        // +0x32
    int domain_ = 2, type_ = 1, protocol_ = 0;   // +0x44
    cRZSocketError error_;                // +0x50
    bool saveOnDelete_ = false;           // +0x60
    cIGZMessageTarget* target_ = nullptr; // +0x68
};

class cRZNetStreamSocket : public cRZNetSocket {
public:
    enum ConnectStatus : int { kNotConnected = 0, kConnected = 2, kListening = 3, kShutDown = 4 };

    cRZNetStreamSocket();   // @ 0x10079c834
    ~cRZNetStreamSocket() override;

    bool CloseSocket() override;                                  // @ 0x10079cbaa
    bool ShutDownSocket(int mode) override;                       // @ 0x10079cbbe
    bool DoMessage(cGZMessage& message) override;                 // @ 0x10079cdcc
    virtual bool IsSocketConnected();                             // @ 0x10079caec
    virtual bool StartListen(int backlog);                        // @ 0x10079cbd2
    virtual bool StopListen();                                    // @ 0x10079cc14
    virtual bool Connect(bool& connected, std::uint32_t timeoutMs);   // @ 0x10079cc98
    virtual bool Connect(const cRZSocketAddress& address, bool& connected, std::uint32_t timeoutMs);   // @ 0x10079cc6a
    virtual int GetConnectStatus() { return connectStatus_; }     // @ 0x10079d69a
    virtual bool Accept(cRZNetStreamSocket& accepted, bool& done);   // @ 0x10079cd00
    virtual bool Accept(std::int64_t& socket, cRZSocketAddress& peer, bool& done);   // @ 0x10079cd60
    virtual bool Read(void* buffer, int& size);                   // @ 0x10079c86c
    virtual bool Peek(void* buffer, int& size);                   // @ 0x10079c8fe
    virtual bool Write(const void* buffer, int& size, int flags);   // @ 0x10079c990
    virtual bool ReadOOB(void* buffer, int& size);                // @ 0x10079c9f4
    virtual bool WriteOOB(const void* buffer, int& size, int flags);   // @ 0x10079ca86

protected:
    bool ReceiveWith(void* buffer, int& size, int flags);   // Peek / ReadOOB
    int connectStatus_ = 0;               // +0x80
};
