// cGZBufferResourceFactory — the resource factory for cGZBufferResource (type 0x62b9da24) plus the helper interface
// cIGZBufferResourceFactory (iid 0xa2c72982, a second vtable at +0x20 of the original object) that loads and saves buffers
// from files and serial records (26 functions at 0x1006162fa..0x100616bb2; thirteen of them are the non-virtual thunks of the
// secondary interface, which `SecondaryInterface` covers here).
//
// Object layout of the original: +0 factory base (0x20 bytes), +0x20 secondary vptr, +0x28 colour type (default {3, 1}),
// +0x30 deque of write versions (starts with one entry, 2).
#pragma once
#include <cstdint>
#include <deque>

#include "gz/Buffer.h"
#include "gz/BufferResource.h"
#include "gz/cGZPersistResourceManager.h"

constexpr std::uint32_t kGZIID_cIGZBufferResourceFactory = 0xa2c72982;

class cIGZBufferResourceFactory : public cIGZUnknown {
public:
    virtual bool LoadFromFile(const cIGZString& path, cIGZBuffer** out, bool, const cGZBufferColorType& colorType, bool) = 0;   // [3]
    virtual bool SaveToFile(const cIGZString& path, cIGZBuffer* buffer) = 0;                                                   // [4]
    virtual bool LoadFromResourceId(int id, cIGZBuffer** out, bool, const cGZBufferColorType& colorType, bool) = 0;          // [5]
    virtual bool LoadFromResourceId(void* module, std::uint32_t id, cIGZBuffer** out) = 0;                                    // [6]
    virtual bool LoadFromSerialRecord(cIGZBuffer** out, cIGZPersistDBSerialRecord* record, bool) = 0;                         // [7]
    virtual bool SaveToSerialRecord(cIGZBuffer* buffer, cIGZPersistDBSerialRecord* record) = 0;                               // [8]
    virtual void SetColorType(const cGZBufferColorType& colorType) = 0;                                                       // [9]
    virtual bool PushWriteVersion(std::uint32_t version) = 0;                                                                 // [10]
    virtual bool PopWriteVersion() = 0;                                                                                       // [11]
    virtual std::uint32_t GetWriteVersion() const = 0;                                                                        // [12]
};

class cGZBufferResourceFactory : public cGZPersistResourceFactory {
public:
    cGZBufferResourceFactory();    // 0x1006162fa
    ~cGZBufferResourceFactory();   // 0x100616b62 / 0x100616bb2

    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x100616398: 1 and 0xa56b8f17 -> this, 0xa2c72982 -> secondary
    // AddRef / Release are the base class's (0x1006163e0 / 0x1006163f8).

    // [9] 0x100616410: only type 0x62b9da24 (a new cGZBufferResource).
    using cGZPersistResourceFactory::CreateInstance;
    bool CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) override;
    bool SerialRead(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) override;    // 0x100616486
    bool SerialWrite(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) override;   // 0x1006164f8

    bool LoadFromFile(const cIGZString& path, cIGZBuffer** out, bool, const cGZBufferColorType& colorType, bool);   // 0x100616586
    bool SaveToFile(const cIGZString& path, cIGZBuffer* buffer);                                                     // 0x1006166a0
    bool LoadFromResourceId(int id, cIGZBuffer** out, bool, const cGZBufferColorType& colorType, bool);            // 0x1006167ce: always false
    bool LoadFromResourceId(void* module, std::uint32_t id, cIGZBuffer** out);                                       // 0x1006167de: always false
    bool LoadFromSerialRecord(cIGZBuffer** out, cIGZPersistDBSerialRecord* record, bool);                            // 0x1006168b6
    bool SaveToSerialRecord(cIGZBuffer* buffer, cIGZPersistDBSerialRecord* record);                                  // 0x100616958
    void SetColorType(const cGZBufferColorType& colorType);                                                          // 0x100616a0c
    bool PushWriteVersion(std::uint32_t version);                                                                    // 0x100616a28
    bool PopWriteVersion();                                                                                          // 0x100616a6c
    std::uint32_t GetWriteVersion() const;                                                                           // 0x100616aa8

    cIGZBufferResourceFactory* AsBufferResourceFactory() { return &secondary_; }

private:
    // The object at +0x20: every method is the matching one above (the thunks subtract 0x20 from `this`).
    struct SecondaryInterface final : cIGZBufferResourceFactory {
        explicit SecondaryInterface(cGZBufferResourceFactory* o) : owner(o) {}
        bool QueryInterface(std::uint32_t iid, void** out) override { return owner->QueryInterface(iid, out); }   // 0x1006163ba
        std::uint32_t AddRef() override { return owner->AddRef(); }                                               // 0x1006163ea
        std::uint32_t Release() override { return owner->Release(); }                                             // 0x100616402
        bool LoadFromFile(const cIGZString& p, cIGZBuffer** o, bool a, const cGZBufferColorType& c, bool b) override { return owner->LoadFromFile(p, o, a, c, b); }   // 0x100616690
        bool SaveToFile(const cIGZString& p, cIGZBuffer* b) override { return owner->SaveToFile(p, b); }          // 0x1006167c0
        bool LoadFromResourceId(int i, cIGZBuffer** o, bool a, const cGZBufferColorType& c, bool b) override { return owner->LoadFromResourceId(i, o, a, c, b); }   // 0x1006167d6
        bool LoadFromResourceId(void* m, std::uint32_t i, cIGZBuffer** o) override { return owner->LoadFromResourceId(m, i, o); }   // 0x10061684a
        bool LoadFromSerialRecord(cIGZBuffer** o, cIGZPersistDBSerialRecord* r, bool a) override { return owner->LoadFromSerialRecord(o, r, a); }   // 0x100616948
        bool SaveToSerialRecord(cIGZBuffer* b, cIGZPersistDBSerialRecord* r) override { return owner->SaveToSerialRecord(b, r); }   // 0x1006169fe
        void SetColorType(const cGZBufferColorType& c) override { owner->SetColorType(c); }                       // 0x100616a1a
        bool PushWriteVersion(std::uint32_t v) override { return owner->PushWriteVersion(v); }                    // 0x100616a4a
        bool PopWriteVersion() override { return owner->PopWriteVersion(); }                                      // 0x100616a88
        std::uint32_t GetWriteVersion() const override { return owner->GetWriteVersion(); }                       // 0x100616ade
        cGZBufferResourceFactory* owner;
    };

    SecondaryInterface secondary_{this};   // +0x20
    cGZBufferColorType colorType_{3, 1};   // +0x28
    std::deque<std::uint32_t> writeVersions_;   // +0x30
};
