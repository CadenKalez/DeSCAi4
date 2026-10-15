// cGZCommandParameterSet — the parameter bag of a framework command (20 functions, 0x1003daf30..0x1003db57e):
// variants keyed by a sparse parameter id (the status value lives under id 0x2b999d2b). Layout: +8 refcount,
// +0x10 map<uint32, cRZVariant>, +0x28 the "null" variant that operator[] hands out for a missing id.
#pragma once
#include <cstdint>
#include <map>

#include "gz/cIGZVariant.h"
#include "rz/cRZVariant.h"

constexpr std::uint32_t kGZIID_cIGZCommandParameterSet = 0x4b8c39e5, kGZIID_cIGZCommandParameterSetAlt = 0x4b8c39e4;
constexpr std::uint32_t kGZStatusParameterID = 0x2b999d2b;
constexpr std::uint32_t kGZAnyParameterID = 0xfffffffe, kGZNoParameterID = 0xffffffff;

class cIGZCommandParameterSet : public cIGZUnknown {
public:
    virtual std::uint32_t GetParameterCount() const = 0;
    virtual void SetParameterCount(std::uint32_t count) = 0;
    virtual cIGZVariant& operator[](std::uint32_t id) = 0;
    virtual cIGZVariant* GetParameter(std::uint32_t id) = 0;
    virtual void SetParameter(std::uint32_t id, const cIGZVariant& value) = 0;
    virtual void AppendParameter(const cIGZVariant& value) = 0;
    virtual void RemoveParameter(std::uint32_t id) = 0;
    virtual void SetStatusParameterValue(std::int32_t value) = 0;
    virtual bool CompareParameterString(std::uint32_t id, const char* text, bool caseSensitive) = 0;
    virtual bool ValidateParameter(std::uint32_t id, std::uint32_t type) = 0;
    virtual std::uint32_t GetFirstParameterID() const = 0;
    virtual std::uint32_t GetNextParameterID(std::uint32_t id) const = 0;
};

class cGZCommandParameterSet : public cIGZCommandParameterSet {
public:
    cGZCommandParameterSet() = default;
    virtual ~cGZCommandParameterSet() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;

    std::uint32_t GetParameterCount() const override { return static_cast<std::uint32_t>(parameters_.size()); }
    void SetParameterCount(std::uint32_t count) override;
    cIGZVariant& operator[](std::uint32_t id) override;
    cIGZVariant* GetParameter(std::uint32_t id) override;
    void SetParameter(std::uint32_t id, const cIGZVariant& value) override;
    void AppendParameter(const cIGZVariant& value) override;
    void RemoveParameter(std::uint32_t id) override;
    void SetStatusParameterValue(std::int32_t value) override;
    bool CompareParameterString(std::uint32_t id, const char* text, bool caseSensitive) override;
    bool ValidateParameter(std::uint32_t id, std::uint32_t type) override;
    std::uint32_t GetFirstParameterID() const override;
    std::uint32_t GetNextParameterID(std::uint32_t id) const override;

private:
    std::uint32_t refCount_ = 0;                          // +8
    std::map<std::uint32_t, cRZVariant> parameters_;     // +0x10
    cRZVariant null_;                                     // +0x28
};
