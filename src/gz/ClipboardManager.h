// The clipboard service. cGZClipboardManager (26 functions, 0x100064ed8..0x10006616a, service id 0x27ebfffd priority
// 0x131334, iid 0x07ec0010) owns clipboards by id (id 0 = the default one, created by Init); cGZClipboardManagerClipboard
// (26 functions, 0x10006439a..0x100064eae, iid 0x87ec000b) holds one typed value per data type. Most manager calls
// take a clipboard id: 0xffffffff = all clipboards, 0xfffffffe = the first clipboard that can answer.
// Quirks kept: the "all clipboards" forms of Set / Clear return false although they act; text is stored as a
// cGZClipboardManagerClipboardData of type 0x67ec001d holding a cRZString; the raw-buffer text calls are stubs that
// just return false; every change posts 0x87ec0025 (this, id, data type) to the message server unless a nested
// change is in progress.
#pragma once
#include <cstdint>
#include <map>
#include <mutex>

#include "gz/SmallObjects.h"
#include "gz/cGZMessageServer.h"
#include "gz/cIGZString.h"
#include "rz/cRZString.h"
#include "rz/cRZSystemService.h"

constexpr std::uint32_t kGZIID_cIGZClipboardManager = 0x07ec0010;
constexpr std::uint32_t kGZIID_cIGZClipboard = 0x87ec000b;
constexpr std::uint32_t kGZSID_cGZClipboardManager = 0x27ebfffd;
constexpr std::int32_t kGZClipboardManagerPriority = 0x131334;
constexpr std::uint32_t kClipboardDataText = 0x67ec001d;
constexpr std::uint32_t kMessageClipboardChanged = 0x87ec0025;
constexpr std::uint32_t kClipboardAll = 0xffffffffu, kClipboardAny = 0xfffffffeu, kClipboardNone = 0xfffffffdu;
constexpr std::uint32_t kGZIID_cIGZStringInterface = 0x089b7dc8;

using gz::cIGZClipboardData;
using gz::cGZClipboardManagerClipboardData;

void SetClipboardMessageServer(cIGZMessageServer* server);

class cIGZClipboard : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                                   // [3]
    virtual bool Shutdown() = 0;                                                               // [4]
    virtual std::uint32_t GetID() const = 0;                                                   // [5]
    virtual void SetID(std::uint32_t id) = 0;                                                  // [6]
    virtual bool GetChangeNotificationEnabled() const = 0;                                     // [7]
    virtual void SetChangeNotificationEnabled(bool enabled) = 0;                               // [8]
    virtual std::uint32_t GetChangeCount() const = 0;                                          // [9]
    virtual bool IsClipboardDataTypeAvailable(std::uint32_t type) = 0;                         // [10]
    virtual std::uint32_t GetAvailableClipboardDataTypes(std::uint32_t* out, std::uint32_t& count) = 0;   // [11]
    virtual cIGZClipboardData* GetClipboardData(std::uint32_t type) = 0;                       // [12]
    virtual bool GetClipboardData(std::uint32_t type, std::uint32_t iid, void** out) = 0;      // [13]
    virtual bool SetClipboardData(cIGZClipboardData* data, bool replaceAll) = 0;               // [14]
    virtual void Clear(std::uint32_t type) = 0;                                                // [15]
    virtual bool GetClipboardText(cIGZString& out) = 0;                                        // [16]
    virtual bool SetClipboardText(const cIGZString& text, bool replaceAll) = 0;                // [17]
    virtual bool GetClipboardText(void* buffer, std::uint32_t& size, std::uint32_t codePage) = 0;   // [18]
    virtual bool SetClipboardText(void* buffer, std::uint32_t size, std::uint32_t codePage, bool replaceAll) = 0;   // [19]
};

class cGZClipboardManagerClipboard final : public cIGZClipboard {
public:
    ~cGZClipboardManagerClipboard() = default;
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override;
    std::uint32_t Release() override;
    bool Init() override;
    bool Shutdown() override;
    std::uint32_t GetID() const override { return id_; }
    void SetID(std::uint32_t id) override { id_ = id; }
    bool GetChangeNotificationEnabled() const override { return notify_; }
    void SetChangeNotificationEnabled(bool enabled) override { notify_ = enabled; }
    std::uint32_t GetChangeCount() const override { return changeCount_; }
    bool IsClipboardDataTypeAvailable(std::uint32_t type) override;
    std::uint32_t GetAvailableClipboardDataTypes(std::uint32_t* out, std::uint32_t& count) override;
    cIGZClipboardData* GetClipboardData(std::uint32_t type) override;
    bool GetClipboardData(std::uint32_t type, std::uint32_t iid, void** out) override;
    bool SetClipboardData(cIGZClipboardData* data, bool replaceAll) override;
    void Clear(std::uint32_t type) override;
    bool GetClipboardText(cIGZString& out) override;
    bool SetClipboardText(const cIGZString& text, bool replaceAll) override;
    bool GetClipboardText(void*, std::uint32_t&, std::uint32_t) override { return false; }
    bool SetClipboardText(void*, std::uint32_t, std::uint32_t, bool) override { return false; }

    void SendChangeNotification(bool initialised);
    void PostDataChangeNotification(std::uint32_t type);

private:
    std::recursive_mutex lock_;
    bool initialised_ = false;                       // +8
    bool notify_ = false;                            // +9
    std::uint32_t refCount_ = 0;                     // +0xc
    std::uint32_t id_ = 0xfffffffdu;                 // +0x10
    int nestedChange_ = 0;                           // +0x14
    std::uint32_t changeCount_ = 0;                  // +0x18
    std::map<std::uint32_t, cIGZClipboardData*> data_;   // +0x28 (a reference each)
};

class cIGZClipboardManager : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                                   // [3]
    virtual bool Shutdown() = 0;                                                               // [4]
    virtual std::uint32_t GetClipboardCount() = 0;                                             // [5]
    virtual cIGZClipboard* GetClipboard(std::uint32_t id) = 0;                                 // [6]
    virtual std::uint32_t GetClipboards(cIGZClipboard** out, std::uint32_t& count) = 0;       // [7]
    virtual bool AddClipboard(cIGZClipboard* clipboard) = 0;                                   // [8]
    virtual bool RemoveClipboard(cIGZClipboard* clipboard) = 0;                                // [9]
    virtual bool RemoveClipboard(std::uint32_t id) = 0;                                        // [10]
    virtual bool IsClipboardDataTypeAvailable(std::uint32_t type, std::uint32_t id) = 0;       // [11]
    virtual std::uint32_t GetAvailableClipboardDataTypes(std::uint32_t* out, std::uint32_t& count, std::uint32_t id) = 0;   // [12]
    virtual std::uint32_t GetClipboardWithDataTypeAvailable(std::uint32_t type) = 0;           // [13]
    virtual bool GetClipboardData(std::uint32_t type, std::uint32_t iid, void** out, std::uint32_t id) = 0;   // [14]
    virtual bool SetClipboardData(cIGZClipboardData* data, bool replaceAll, std::uint32_t id) = 0;   // [15]
    virtual bool ClearClipboard(std::uint32_t type, std::uint32_t id) = 0;                     // [16]
    virtual bool GetClipboardText(cIGZString& out, std::uint32_t id) = 0;                      // [17]
    virtual bool SetClipboardText(const cIGZString& text, bool replaceAll, std::uint32_t id) = 0;   // [18]
    virtual bool GetClipboardText(void* buffer, std::uint32_t& size, std::uint32_t codePage, std::uint32_t id) = 0;   // [19]
    virtual bool SetClipboardText(void* buffer, std::uint32_t size, std::uint32_t codePage, bool replaceAll, std::uint32_t id) = 0;   // [20]
};

class cGZClipboardManager final : public cIGZClipboardManager, public cRZSystemService {
public:
    cGZClipboardManager() : cRZSystemService(kGZSID_cGZClipboardManager, kGZClipboardManagerPriority) {}
    ~cGZClipboardManager() override = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override;
    std::uint32_t Release() override;
    bool Init() override;
    bool Shutdown() override;
    std::uint32_t GetClipboardCount() override;
    cIGZClipboard* GetClipboard(std::uint32_t id) override;
    std::uint32_t GetClipboards(cIGZClipboard** out, std::uint32_t& count) override;
    bool AddClipboard(cIGZClipboard* clipboard) override;
    bool RemoveClipboard(cIGZClipboard* clipboard) override;
    bool RemoveClipboard(std::uint32_t id) override;
    bool IsClipboardDataTypeAvailable(std::uint32_t type, std::uint32_t id) override;
    std::uint32_t GetAvailableClipboardDataTypes(std::uint32_t* out, std::uint32_t& count, std::uint32_t id) override;
    std::uint32_t GetClipboardWithDataTypeAvailable(std::uint32_t type) override;
    bool GetClipboardData(std::uint32_t type, std::uint32_t iid, void** out, std::uint32_t id) override;
    bool SetClipboardData(cIGZClipboardData* data, bool replaceAll, std::uint32_t id) override;
    bool ClearClipboard(std::uint32_t type, std::uint32_t id) override;
    bool GetClipboardText(cIGZString& out, std::uint32_t id) override;
    bool SetClipboardText(const cIGZString& text, bool replaceAll, std::uint32_t id) override;
    bool GetClipboardText(void* buffer, std::uint32_t& size, std::uint32_t codePage, std::uint32_t id) override;
    bool SetClipboardText(void* buffer, std::uint32_t size, std::uint32_t codePage, bool replaceAll, std::uint32_t id) override;

private:
    cIGZClipboard* Lookup(std::uint32_t id);     // without taking the lock again
    std::recursive_mutex lock_;
    bool initialised_ = false;                   // +0x29
    std::uint32_t refCount_ = 0;                 // +0x2c
    std::map<std::uint32_t, cIGZClipboard*> clipboards_;   // +0x30 (a reference each)
};
