// Exemplars ("property sets" of game objects) and cohorts. Recovered from Sim City 4 Deluxe
// Editionsub 1.2.1: cSCResExemplar (vtable 0x100a95bb0), cSCResExemplarCohort (0x100a95980),
// their factories (0x100a95de0 / 0x100a95e60), cSCResProperty (0x100a69ef0) and
// cSCExemplarPropertyHolder (0x100a6a580).
//
// An exemplar (resource type 0x6534284a) is an id-sorted property list plus an optional parent
// cohort (type 0x05342861); a cohort is the same with an optional parent cohort. Lookups see the
// merged view (own properties override the parent chain's); `IsPropertyLocal` tells them apart. The
// merged view is cached and rebuilt whenever the parent or a local property changes.
//
// Binary file layout ("EQZB1###" / "CQZB1###", 8 bytes): parent key (type, group, instance — all
// zero = none), u32 property count, then per property: u32 id, u8 kind (0 = value follows as a
// variant, 1 = a resource key follows and the value is that resource), value. Text layout
// ("EQZT1###\r\n"): `ParentCohort=Key:{group,instance,type}`, `PropCount=0x..`, then one line per
// property `0xID:{"Name"}=Type:count:{v,v,...}` (types Bool Uint8 Sint8 Uint16 Sint16 Uint32 Sint32
// Uint64 Sint64 UintPtr Float32 String; hex integers, floats with trailing zeros trimmed).
//
// Quirks kept: the factories' default write mode is text (SetWriteMode(false) selects binary);
// the exemplar/cohort cIGZSerializable Read/Write are stubs returning false; a property id that is
// in the factory's skip set is parsed but not stored; the text "Key:{...}" order is group, instance,
// type; Uint64/Sint64/UintPtr array elements are printed without the "0x" prefix.
#pragma once
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gz/cGZPersistResourceManager.h"
#include "gz/cIGZSerializable.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZUnknownList.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"
#include "sc4/SCProperty.h"

namespace sc4 {

constexpr std::uint32_t kResourceTypeExemplar = 0x6534284a;
constexpr std::uint32_t kResourceTypeCohort = 0x05342861;
constexpr std::uint32_t kGZIID_cISCResExemplar = 0xa52160f5;
constexpr std::uint32_t kGZIID_cISCResExemplarCohort = 0x452160fb;
constexpr std::uint32_t kGZIID_cISCResProperty = 0xe52184ab;
constexpr std::uint32_t kExemplarChangedMessage = 0xc53d10aa;
constexpr std::uint32_t kCohortChangedMessage = 0x853d11f1;
constexpr std::uint32_t kPropertyParentCohort = 0x6a871b82;   // "ParentCohort" key property (unused here)

// Change notifications go through this sink (SendMessageA(type, key, object) in the original).
using ExemplarNotifyFn = void (*)(std::uint32_t message, const cGZPersistResourceKey& key, cIGZSerializable* object);
void SetExemplarNotifier(ExemplarNotifyFn fn);

class cISCResExemplarCohort;

// cISCResProperty: a property whose value is a persisted resource referenced by key.
class cSCResProperty final : public cISCProperty {
public:
    cSCResProperty() = default;
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    std::uint32_t GetPropertyID() const override { return id_; }
    void SetPropertyID(std::uint32_t id) override { id_ = id; }
    const cIGZVariant* GetPropertyValue() const override { return &value_; }
    cIGZVariant* GetPropertyValue() override;                        // loads the resource on demand
    bool SetPropertyValue(const cIGZVariant& value) override;       // only an object variant (0x8010)
    bool SetPropertyValue(cIGZPersistResource* resource);
    bool Write(cIGZOStream& stream) const override { stream.SetUint32(id_); return !stream.GetError(); }
    bool Read(cIGZIStream& stream) override { stream.GetUint32(id_); return !stream.GetError(); }

    cGZPersistResourceKey GetPropertyKey() const { return key_; }
    bool SetPropertyKey(const cGZPersistResourceKey& key) { key_ = key; return true; }

private:
    cRZVariant value_;
    cGZPersistResourceKey key_;
    std::uint32_t id_ = 0;
    std::uint32_t refCount_ = 0;
};

// Properties of exemplars/cohorts, with the parent-chain merge.
class cSCExemplarProperties : public cSCPropertyHolder {
public:
    // merged lookups (the property holder interface of an exemplar/cohort)
    bool HasProperty(std::uint32_t id) const override;
    bool GetPropertyList(cIGZUnknownList** out) const override;
    cISCProperty* GetProperty(std::uint32_t id) const override;
    using cSCPropertyHolder::GetProperty;
    void AddProperty(cISCProperty* property, bool notify) override;
    bool RemoveProperty(std::uint32_t id, bool notify) override;
    void RemoveAllProperties(bool notify) override;
    void CompactProperties() override;

    bool IsPropertyLocal(std::uint32_t id) const { return cSCPropertyHolder::HasProperty(id); }
    std::size_t LocalCount() const { return Count(); }
    // rebuilds the merged map (optionally into `target` instead of the object's own cache)
    void GenerateCachedMap();

protected:
    virtual cISCPropertyHolder* ParentProperties() const = 0;     // the parent cohort's merged view
    virtual void CompactParent() = 0;
    virtual void NotifyChanged() = 0;

private:
    struct Entry {
        std::uint32_t id;
        cRZAutoRefCount<cISCProperty> property;
    };
    std::vector<Entry>::iterator LowerBound(std::uint32_t id);
    std::vector<Entry>::const_iterator LowerBound(std::uint32_t id) const;
    std::vector<Entry> cache_;
};

class cISCResExemplarCohort : public cIGZSerializable {
public:
    virtual cISCResExemplarCohort* GetParent() = 0;
    virtual void SetParent(cISCResExemplarCohort* parent, bool notify) = 0;
    virtual cISCPropertyHolder* AsISCPropertyHolder() = 0;
    virtual bool IsPropertyLocal(std::uint32_t id) = 0;
    virtual bool GetKey(cGZPersistResourceKey& key) = 0;
    virtual bool SetKey(const cGZPersistResourceKey& key) = 0;
    virtual void CompactProperties() = 0;
    virtual void GenerateCachedMap() = 0;
};

class cISCResExemplar : public cIGZSerializable {
public:
    virtual cISCPropertyHolder* AsISCPropertyHolder() = 0;
    virtual bool IsPropertyLocal(std::uint32_t id) = 0;
    virtual cISCResExemplarCohort* GetCohort() = 0;
    virtual void SetCohort(cISCResExemplarCohort* cohort, bool notify) = 0;
    virtual bool GetKey(cGZPersistResourceKey& key) = 0;
    virtual bool SetKey(const cGZPersistResourceKey& key) = 0;
    virtual void CompactProperties() = 0;
    virtual void GenerateCachedMap() = 0;
};

// Shared implementation of exemplar and cohort (they differ in interface ids, the notification
// message and the parent's type).
class cSCResExemplar final : public cGZPersistResource, public cISCResExemplar, public cSCExemplarProperties {
public:
    cSCResExemplar() = default;
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZPersistResource::AddRef(); }
    std::uint32_t Release() override { return cGZPersistResource::Release(); }

    // cIGZSerializable (stubs in the original)
    bool Write(cIGZOStream&) const override { return false; }
    bool Read(cIGZIStream&) override { return false; }
    std::uint32_t GetGZCLSID() const override { return 0; }

    cISCPropertyHolder* AsISCPropertyHolder() override { return static_cast<cSCExemplarProperties*>(this); }
    bool IsPropertyLocal(std::uint32_t id) override { return cSCExemplarProperties::IsPropertyLocal(id); }
    cISCResExemplarCohort* GetCohort() override { return cohort_.get(); }
    void SetCohort(cISCResExemplarCohort* cohort, bool notify) override;
    bool GetKey(cGZPersistResourceKey& key) override { key = cGZPersistResource::GetKey(); return true; }
    bool SetKey(const cGZPersistResourceKey& key) override { cGZPersistResource::SetKey(key); return true; }
    void CompactProperties() override { cSCExemplarProperties::CompactProperties(); }
    void GenerateCachedMap() override { cSCExemplarProperties::GenerateCachedMap(); }

    // property holder view (disambiguate the shared names)
    using cSCExemplarProperties::AddProperty;
    using cSCExemplarProperties::GetProperty;
    using cSCExemplarProperties::HasProperty;
    using cSCExemplarProperties::RemoveAllProperties;
    using cSCExemplarProperties::RemoveProperty;
    void DebugInstanceTag(cIGZString& out) const;

public:
    ~cSCResExemplar() override = default;
    cISCPropertyHolder* ParentProperties() const override;
    void CompactParent() override;
    void NotifyChanged() override;

private:
    cRZAutoRefCount<cISCResExemplarCohort> cohort_;
};

class cSCResExemplarCohort final : public cGZPersistResource, public cISCResExemplarCohort, public cSCExemplarProperties {
public:
    cSCResExemplarCohort() = default;
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZPersistResource::AddRef(); }
    std::uint32_t Release() override { return cGZPersistResource::Release(); }

    bool Write(cIGZOStream&) const override { return false; }
    bool Read(cIGZIStream&) override { return false; }
    std::uint32_t GetGZCLSID() const override { return 0; }

    cISCResExemplarCohort* GetParent() override { return parent_.get(); }
    void SetParent(cISCResExemplarCohort* parent, bool notify) override;
    cISCPropertyHolder* AsISCPropertyHolder() override { return static_cast<cSCExemplarProperties*>(this); }
    bool IsPropertyLocal(std::uint32_t id) override { return cSCExemplarProperties::IsPropertyLocal(id); }
    bool GetKey(cGZPersistResourceKey& key) override { key = cGZPersistResource::GetKey(); return true; }
    bool SetKey(const cGZPersistResourceKey& key) override { cGZPersistResource::SetKey(key); return true; }
    void CompactProperties() override { cSCExemplarProperties::CompactProperties(); }
    void GenerateCachedMap() override { cSCExemplarProperties::GenerateCachedMap(); }

    using cSCExemplarProperties::AddProperty;
    using cSCExemplarProperties::GetProperty;
    using cSCExemplarProperties::HasProperty;
    using cSCExemplarProperties::RemoveAllProperties;
    using cSCExemplarProperties::RemoveProperty;

public:
    ~cSCResExemplarCohort() override = default;
    cISCPropertyHolder* ParentProperties() const override;
    void CompactParent() override;
    void NotifyChanged() override;

private:
    cRZAutoRefCount<cISCResExemplarCohort> parent_;
};

// ---- factories ----------------------------------------------------------------------------------
class cSCResExemplarFactoryBase : public cGZPersistResourceFactory {
public:
    using cGZPersistResourceFactory::CreateInstance;
    bool SerialRead(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) override;
    bool SerialWrite(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) override;

    // false = binary, true = text (the default)
    void SetWriteMode(bool text) { textWrite_ = text; }
    // human-readable property names used by the text writer (the original reads them from the
    // [Properties] section of the settings ini); unknown ids are written as "unknown"
    static void SetPropertyNames(std::unordered_map<std::uint32_t, std::string> names);
    static bool GetPropertyReadableName(std::uint32_t id, std::string& out);
    void AddSkippedProperty(std::uint32_t id) { skipped_.insert(id); }

protected:
    virtual std::uint32_t ResourceType() const = 0;
    virtual const char* BinaryMagic() const = 0;   // 8 bytes
    virtual const char* TextMagic() const = 0;     // "XQZT1###" (first 6 + checked as 'T1')
    // access to the resource-specific parts
    virtual cISCPropertyHolder* PropertyHolder(cIGZPersistResource& resource) = 0;
    virtual void SetParentFromKey(cIGZPersistResource& resource, cISCResExemplarCohort* parent) = 0;
    virtual cISCResExemplarCohort* ParentOf(cIGZPersistResource& resource) = 0;
    virtual void ClearProperties(cIGZPersistResource& resource) = 0;
    virtual void AddLocalProperty(cIGZPersistResource& resource, cISCProperty* property) = 0;
    virtual void Finish(cIGZPersistResource& resource) = 0;                    // GenerateCachedMap + Compact
    virtual std::vector<cISCProperty*> LocalProperties(cIGZPersistResource& resource) = 0;

    bool textWrite_ = true;                                      // +0x20
    std::unordered_set<std::uint32_t> skipped_;                  // +0x28
};

class cSCResExemplarFactory final : public cSCResExemplarFactoryBase {
public:
    bool CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) override;

protected:
    std::uint32_t ResourceType() const override { return kResourceTypeExemplar; }
    const char* BinaryMagic() const override { return "EQZB1###"; }
    const char* TextMagic() const override { return "EQZT1###"; }
    cISCPropertyHolder* PropertyHolder(cIGZPersistResource& resource) override;
    void SetParentFromKey(cIGZPersistResource& resource, cISCResExemplarCohort* parent) override;
    cISCResExemplarCohort* ParentOf(cIGZPersistResource& resource) override;
    void ClearProperties(cIGZPersistResource& resource) override;
    void AddLocalProperty(cIGZPersistResource& resource, cISCProperty* property) override;
    void Finish(cIGZPersistResource& resource) override;
    std::vector<cISCProperty*> LocalProperties(cIGZPersistResource& resource) override;
};

class cSCResExemplarCohortFactory final : public cSCResExemplarFactoryBase {
public:
    bool CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) override;

protected:
    std::uint32_t ResourceType() const override { return kResourceTypeCohort; }
    const char* BinaryMagic() const override { return "CQZB1###"; }
    const char* TextMagic() const override { return "CQZT1###"; }
    cISCPropertyHolder* PropertyHolder(cIGZPersistResource& resource) override;
    void SetParentFromKey(cIGZPersistResource& resource, cISCResExemplarCohort* parent) override;
    cISCResExemplarCohort* ParentOf(cIGZPersistResource& resource) override;
    void ClearProperties(cIGZPersistResource& resource) override;
    void AddLocalProperty(cIGZPersistResource& resource, cISCProperty* property) override;
    void Finish(cIGZPersistResource& resource) override;
    std::vector<cISCProperty*> LocalProperties(cIGZPersistResource& resource) override;
};

// ---- cSCExemplarPropertyHolder ---------------------------------------------------------------------
// A property holder whose own properties override those of a default exemplar. Written with a
// reference to the exemplar's key; an exemplar flagged with property 0x6a871b82 is embedded in the
// save (its record is written into the same segment).
constexpr std::uint32_t kGZIID_cISCExemplarPropertyHolder = 0x0ac2b5f7;
constexpr std::uint32_t kPropertyEmbedExemplarInSave = 0x6a871b82;

class cISCExemplarPropertyHolder : public cIGZUnknown {
public:
    virtual void SetDefaultExemplar(cISCResExemplar* exemplar) = 0;           // [3]
    virtual cISCResExemplar* GetDefaultExemplar() const = 0;                  // [4]
};

class cSCExemplarPropertyHolder : public cSCPropertyHolder, public cISCExemplarPropertyHolder {
public:
    cSCExemplarPropertyHolder() = default;
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cSCPropertyHolder::AddRef(); }
    std::uint32_t Release() override { return cSCPropertyHolder::Release(); }
    bool HasProperty(std::uint32_t id) const override;
    bool GetPropertyList(cIGZUnknownList** out) const override;
    cISCProperty* GetProperty(std::uint32_t id) const override;
    using cSCPropertyHolder::GetProperty;
    void EnumProperties(void (*callback)(cISCProperty*, void*), void* context) const override;
    void EnumProperties(bool (*filter)(cISCProperty*, void*), void (*callback)(cISCProperty*, void*),
                        void* context) const override;
    virtual bool Write(cIGZOStream& stream) const;
    virtual bool Read(cIGZIStream& stream);
    void SetDefaultExemplar(cISCResExemplar* exemplar) override;
    cISCResExemplar* GetDefaultExemplar() const override { return defaultExemplar_.get(); }
    // The key read from a stream, kept even when no resource manager could resolve it (port addition:
    // lets tools identify a building's exemplar without loading it). Write still follows the exemplar.
    const cGZPersistResourceKey& DefaultExemplarKey() const { return defaultExemplarKey_; }

private:
    cGZPersistResourceKey defaultExemplarKey_;
    cRZAutoRefCount<cISCResExemplar> defaultExemplar_;     // +0x30
};

}  // namespace sc4
