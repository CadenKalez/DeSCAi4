// cGZPersistBufferResourceFactory — the factory of cGZPersistBufferResource (type 0x856ddbac) and the buffer helper interface
// cIGZPersistBufferResourceFactory (iid 0x656ddc26, a second vtable at +0x20 of the original object); 25 functions at
// 0x100611c9c..0x1006148b4, eleven of them non-virtual thunks of that second interface (covered by `Secondary`).
//
// Serial record formats read by LoadFromSerialRecord: version 1 {version, size, width, height, colour type, raw rows},
// version 2 the same with RefPack-compressed rows (a 4 byte length in front), and the legacy version 0x856ddbac
// {version, width, height, u32 pixels}. SaveToSerialRecord writes version 2 (compressed) or 1. Saving to a resource
// (SerialWrite), to a file and loading by resource id are not supported (they return false).
//
// Object layout: +0 factory base (0x20 bytes), +0x20 second interface, +0x28 colour type (default {1, 0}).
#pragma once
#include <cstdint>

#include "gz/PersistBufferResource.h"

constexpr std::uint32_t kGZIID_cIGZPersistBufferResourceFactory = 0x656ddc26;

class cIGZPersistBufferResourceFactory : public cIGZUnknown {
public:
    virtual bool LoadFromFile(const cIGZString& path, cIGZBuffer** out, bool, cGZBufferColorType colorType, bool) = 0;   // [3]
    virtual bool SaveToFile(const cIGZString& path, cIGZBuffer** buffer) = 0;                                           // [4]
    virtual bool LoadFromResourceId(int id, cIGZBuffer** out, bool, cGZBufferColorType colorType, bool) = 0;            // [5]
    virtual bool LoadFromSerialRecord(cIGZBuffer** out, cIGZPersistDBSerialRecord* record, bool) = 0;                   // [6]
    virtual bool SaveToSerialRecord(cIGZBuffer* buffer, cIGZPersistDBSerialRecord* record, bool compress) = 0;          // [7]
    virtual void SetColorType(cGZBufferColorType colorType) = 0;                                                         // [8]
};

class cGZPersistBufferResourceFactory : public cGZPersistResourceFactory {
public:
    explicit cGZPersistBufferResourceFactory(IPersistBufferHost* host);   // 0x100611c9c
    ~cGZPersistBufferResourceFactory();                                   // 0x100614872 / 0x10061487c

    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x100611cce: 1 / 0xa56b8f17 -> this, 0x656ddc26 -> second interface
    // AddRef / Release: the base class's (0x100611d16, 0x100611d2e)

    using cGZPersistResourceFactory::CreateInstance;
    bool CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) override;   // 0x100611d46
    // 0x100611d6c: `flags` is the colour type id (the 3 argument form passes the factory's own)
    bool CreateInstance(std::uint32_t type, std::uint32_t iid, void** out, std::uint32_t flags, cIGZUnknown* owner) override;
    bool SerialRead(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) override;    // 0x100611e16
    bool SerialWrite(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) override;   // 0x10061201a: false

    bool LoadFromFile(const cIGZString& path, cIGZBuffer** out, bool, cGZBufferColorType colorType, bool);   // 0x100612022
    bool SaveToFile(const cIGZString&, cIGZBuffer**) { return false; }                                       // 0x1006124b4
    bool LoadFromResourceId(int, cIGZBuffer**, bool, cGZBufferColorType, bool) { return false; }             // 0x1006124c4
    bool LoadFromSerialRecord(cIGZBuffer** out, cIGZPersistDBSerialRecord* record, bool);                   // 0x1006124e0
    bool SaveToSerialRecord(cIGZBuffer* buffer, cIGZPersistDBSerialRecord* record, bool compress);          // 0x100612a20
    void SetColorType(cGZBufferColorType colorType) { colorType_ = colorType; }                              // 0x1006148aa

    cIGZPersistBufferResourceFactory* AsBufferFactory() { return &secondary_; }

private:
    struct Secondary final : cIGZPersistBufferResourceFactory {
        explicit Secondary(cGZPersistBufferResourceFactory* o) : owner(o) {}
        bool QueryInterface(std::uint32_t iid, void** out) override { return owner->QueryInterface(iid, out); }   // 0x100611cf0
        std::uint32_t AddRef() override { return owner->AddRef(); }                                               // 0x100611d20
        std::uint32_t Release() override { return owner->Release(); }                                             // 0x100611d38
        bool LoadFromFile(const cIGZString& p, cIGZBuffer** o, bool a, cGZBufferColorType c, bool b) override { return owner->LoadFromFile(p, o, a, c, b); }   // 0x1006124a2
        bool SaveToFile(const cIGZString& p, cIGZBuffer** b) override { return owner->SaveToFile(p, b); }          // 0x1006124bc
        bool LoadFromResourceId(int i, cIGZBuffer** o, bool a, cGZBufferColorType c, bool b) override { return owner->LoadFromResourceId(i, o, a, c, b); }   // 0x1006124cc
        bool LoadFromSerialRecord(cIGZBuffer** o, cIGZPersistDBSerialRecord* r, bool a) override { return owner->LoadFromSerialRecord(o, r, a); }   // 0x100612a14
        bool SaveToSerialRecord(cIGZBuffer* b, cIGZPersistDBSerialRecord* r, bool c) override { return owner->SaveToSerialRecord(b, r, c); }   // 0x100612d32
        void SetColorType(cGZBufferColorType c) override { owner->SetColorType(c); }                                // 0x1006148b4
        cGZPersistBufferResourceFactory* owner;
    };

    IPersistBufferHost* host_;
    Secondary secondary_{this};            // +0x20
    cGZBufferColorType colorType_{1, 0};   // +0x28
};
