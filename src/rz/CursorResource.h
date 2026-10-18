// cRZCursorResource (11 functions, 0x1006170a0..0x100617354, resource type 0x686aa4b0, view iid 0x88696706) and
// its factory cRZCursorResourceFactory (10 functions, 0x100617366..0x100617550, view iid 0x08696779): a
// persistent resource that wraps one cIGZCursor (+0x28). The constructor asks the COM registry for the cursor
// object (class 0x286a98a8, iid 0x834c471e); QueryInterface first lets the held cursor answer. Loading from a
// serial record and writing are not supported (both just fail). cSC4AudioCompletion lives in audio/.
#pragma once
#include <cstdint>

#include "gz/WinInterfaces.h"
#include "gz/cGZPersistResourceManager.h"
#include "gz/cIGZPersistDBSerialRecord.h"

constexpr std::uint32_t kRZResourceTypeCursor = 0x686aa4b0;
constexpr std::uint32_t kGZIID_cIRZCursorResource = 0x88696706;
constexpr std::uint32_t kGZIID_cIRZCursorResourceFactory = 0x08696779;
constexpr std::uint32_t kGZCLSID_RZCursor = 0x286a98a8, kGZIID_RZCursor = 0x834c471e;

// GZCOM()->GetCOMObject(0x286a98a8, 0x834c471e, &cursor)
class IRZCursorSource {
public:
    virtual ~IRZCursorSource() = default;
    virtual cIGZCursor* CreateCursor() = 0;
};
IRZCursorSource* RZCursorSource();
void SetRZCursorSource(IRZCursorSource* source);

class cIRZCursorResource : public cIGZUnknown {
public:
    virtual bool SetCursor(cIGZCursor* cursor) = 0;
    virtual bool GetCursor(cIGZCursor** out) const = 0;
    virtual bool GetCursorAs(std::uint32_t iid, void** out) const = 0;
    virtual bool LoadFromSerialRecord(cIGZPersistDBSerialRecord* record) = 0;
};

class cRZCursorResource final : public cGZPersistResource, public cIRZCursorResource {
public:
    cRZCursorResource();
    ~cRZCursorResource() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZPersistResource::AddRef(); }
    std::uint32_t Release() override { return cGZPersistResource::Release(); }
    bool SetCursor(cIGZCursor* cursor) override;
    bool GetCursor(cIGZCursor** out) const override;
    bool GetCursorAs(std::uint32_t iid, void** out) const override;
    bool LoadFromSerialRecord(cIGZPersistDBSerialRecord*) override { return false; }

private:
    cIGZCursor* cursor_ = nullptr;   // +0x28
};

class cIRZCursorResourceFactory : public cIGZUnknown {
public:
    virtual bool LoadFromSerialRecord(cIGZCursor** cursor, cIGZPersistDBSerialRecord* record) = 0;
};

class cRZCursorResourceFactory final : public cGZPersistResourceFactory, public cIRZCursorResourceFactory {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZPersistResourceFactory::AddRef(); }
    std::uint32_t Release() override { return cGZPersistResourceFactory::Release(); }
    using cGZPersistResourceFactory::CreateInstance;
    bool CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) override;
    bool SerialRead(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) override;
    bool SerialWrite(cIGZPersistResource&, cIGZPersistDBSerialRecord&) override { return false; }
    bool LoadFromSerialRecord(cIGZCursor**, cIGZPersistDBSerialRecord*) override { return false; }
};
