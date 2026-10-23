// Property bag classes of the game (exemplar properties): cISCProperty / cSCProperty (id + value)
// and cISCPropertyHolder / cSCPropertyHolder (id-sorted collection). Recovered from Sim City 4
// Deluxe Editionsub 1.2.1 (cSCProperty vtable 0x100a69a40, cSCPropertyHolder 0x100a6a4b0).
//
// cSCProperty is a property id plus an embedded cRZVariant; the original exposes the variant
// through a second base. Here the property owns a cRZVariant and answers QueryInterface for
// cIGZVariant with it (AddRef on the property). Serialization of a holder is: u16 version (2),
// u32 count, then per property u32 id + the property's own record (its id again); property values
// are not part of this stream and a property whose value is empty (or an empty object) is never
// added when reading — quirks kept.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZUnknownList.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZVariant.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISCProperty = 0x452160ec;
constexpr std::uint32_t kGZIID_cISCPropertyHolder = 0x25216283;

class cISCProperty : public cIGZUnknown {
public:
    virtual std::uint32_t GetPropertyID() const = 0;                       // [3]
    virtual void SetPropertyID(std::uint32_t id) = 0;                      // [4]
    virtual const cIGZVariant* GetPropertyValue() const = 0;               // [5]
    virtual cIGZVariant* GetPropertyValue() = 0;                           // [6]
    virtual bool SetPropertyValue(const cIGZVariant& value) = 0;           // [7]
    virtual bool Write(cIGZOStream& stream) const = 0;                     // [8] writes the id, then the value when the stream is a DB segment stream
    virtual bool Read(cIGZIStream& stream) = 0;                            // [9] reads the id, then the value (DB segment streams)
    virtual ~cISCProperty() = default;                                     // [10],[11]
};

class cSCProperty final : public cISCProperty {
public:
    explicit cSCProperty(std::uint32_t id = 0) : id_(id) {}
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }                // 16-bit in the original
    std::uint32_t Release() override;

    std::uint32_t GetPropertyID() const override { return id_; }
    void SetPropertyID(std::uint32_t id) override { id_ = id; }
    const cIGZVariant* GetPropertyValue() const override { return &value_; }
    cIGZVariant* GetPropertyValue() override { return &value_; }
    bool SetPropertyValue(const cIGZVariant& value) override { value_.CopyFrom(value); return true; }
    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;

private:
    cRZVariant value_;
    std::uint16_t refCount_ = 0;
    std::uint32_t id_;
};

class cISCPropertyHolder : public cIGZUnknown {
public:
    virtual bool HasProperty(std::uint32_t id) const = 0;                                         // [3]
    virtual bool GetPropertyList(cIGZUnknownList** out) const = 0;                                // [4]
    virtual cISCProperty* GetProperty(std::uint32_t id) const = 0;                                // [5]
    virtual bool GetProperty(std::uint32_t id, std::uint32_t& value) const = 0;                   // [6]
    virtual bool GetProperty(std::uint32_t id, cIGZString& value) const = 0;                      // [7]
    virtual bool GetProperty(std::uint32_t id, std::uint32_t iid, void** out) const = 0;          // [8]
    virtual bool GetProperty(std::uint32_t id, void* data, std::uint32_t& size) const = 0;        // [9]
    virtual void AddProperty(cISCProperty* property, bool replace) = 0;                           // [10]
    virtual void AddProperty(std::uint32_t id, const cIGZVariant* value, bool replace) = 0;       // [11]
    virtual void AddProperty(std::uint32_t id, std::uint32_t value, bool replace) = 0;            // [12]
    virtual void AddProperty(std::uint32_t id, const cIGZString& value, bool replace) = 0;        // [13]
    virtual void AddProperty(std::uint32_t id, cIGZUnknown* value, bool replace) = 0;             // [14]
    virtual void AddProperty(std::uint32_t id, const void* data, std::uint32_t size, bool replace) = 0;  // [15]
    virtual void CopyAddProperty(cISCProperty* property, bool replace) = 0;                       // [16]
    virtual bool RemoveProperty(std::uint32_t id, bool notify) = 0;                               // [17]
    virtual void RemoveAllProperties(bool notify) = 0;                                            // [18]
    virtual void EnumProperties(void (*callback)(cISCProperty*, void*), void* context) const = 0;  // [19]
    virtual void EnumProperties(bool (*filter)(cISCProperty*, void*), void (*callback)(cISCProperty*, void*),
                                void* context) const = 0;                                         // [20]
    virtual void CompactProperties() = 0;                                                         // [21]
    virtual ~cISCPropertyHolder() = default;                                                      // [22],[23]
};

class cSCPropertyHolder : public cISCPropertyHolder {
public:
    cSCPropertyHolder() = default;
    cSCPropertyHolder(const cSCPropertyHolder& other);       // copies the property references

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    bool HasProperty(std::uint32_t id) const override;
    bool GetPropertyList(cIGZUnknownList** out) const override;
    cISCProperty* GetProperty(std::uint32_t id) const override;
    bool GetProperty(std::uint32_t id, std::uint32_t& value) const override;
    bool GetProperty(std::uint32_t id, cIGZString& value) const override;
    bool GetProperty(std::uint32_t id, std::uint32_t iid, void** out) const override;
    bool GetProperty(std::uint32_t id, void* data, std::uint32_t& size) const override;
    using cISCPropertyHolder::GetProperty;
    void AddProperty(cISCProperty* property, bool replace) override;
    void AddProperty(std::uint32_t id, const cIGZVariant* value, bool replace) override;
    void AddProperty(std::uint32_t id, std::uint32_t value, bool replace) override;
    void AddProperty(std::uint32_t id, const cIGZString& value, bool replace) override;
    void AddProperty(std::uint32_t id, cIGZUnknown* value, bool replace) override;
    void AddProperty(std::uint32_t id, const void* data, std::uint32_t size, bool replace) override;
    void CopyAddProperty(cISCProperty* property, bool replace) override;
    bool RemoveProperty(std::uint32_t id, bool notify) override;
    void RemoveAllProperties(bool) override { items_.clear(); }
    void EnumProperties(void (*callback)(cISCProperty*, void*), void* context) const override;
    void EnumProperties(bool (*filter)(cISCProperty*, void*), void (*callback)(cISCProperty*, void*),
                        void* context) const override;
    void CompactProperties() override { items_.shrink_to_fit(); }

    bool Write(cIGZOStream& stream) const;
    bool Read(cIGZIStream& stream);
    std::size_t Count() const { return items_.size(); }
    std::uint32_t RefCount() const { return refCount_; }   // port addition: automata run Shutdown at the last release

private:
    struct Entry {
        std::uint32_t id;
        cRZAutoRefCount<cISCProperty> property;
    };
    std::vector<Entry>::const_iterator Find(std::uint32_t id) const;
    cISCProperty* FindOrCreate(std::uint32_t id, bool& created);
    void InsertSorted(std::uint32_t id, cISCProperty* property);

    std::vector<Entry> items_;   // sorted by id
    std::uint32_t refCount_ = 0;
};

}  // namespace sc4
