// cSC4AdviceSubject — what an advisor message is about (41 functions, 0x10044ba4c..0x10044c0c0): an optional
// city location (clamped to the city), two data words and an optional object reference. Class id 0x6a26d049,
// interface 0x6a26d050, version 4 record: s8 hasLocation, [float x, float z], u32 data1, u32 data2, then the
// object as a nested serializable. Layout: +0x20 hasLocation, +0x24/+0x28 location, +0x2c/+0x30 data, +0x38 object.
#pragma once
#include <cstdint>

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZSerializable.h"
#include "rz/cRZUnknown.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4AdviceSubject = 0x6a26d049;
constexpr std::uint32_t kGZIID_cISC4AdviceSubject = 0x6a26d050;

class IAdviceSubjectHost {
public:
    virtual ~IAdviceSubjectHost() = default;
    virtual std::int32_t CityCellsX() = 0;   // spCity vslot 0x2e8
    virtual std::int32_t CityCellsZ() = 0;   // spCity vslot 0x2f0
};
IAdviceSubjectHost* AdviceSubjectHost();
void SetAdviceSubjectHost(IAdviceSubjectHost* host);

class cISC4AdviceSubject : public cIGZUnknown {
public:
    virtual void SetLocation(float x, float z) = 0;
    virtual float GetLocationX() = 0;
    virtual float GetLocationZ() = 0;
    virtual bool HasLocation() = 0;
    virtual bool SetObject(cIGZUnknown* object) = 0;
    virtual cIGZUnknown* GetObjectA() = 0;
    virtual std::uint32_t GetData1() const = 0;
    virtual void SetData1(std::uint32_t value) = 0;
    virtual std::uint32_t GetData2() const = 0;
    virtual void SetData2(std::uint32_t value) = 0;
    virtual bool Clone(cISC4AdviceSubject** out) = 0;
};

class cSC4AdviceSubject : public cRZUnknown, public cIGZSerializable, public cISC4AdviceSubject {
public:
    cSC4AdviceSubject() = default;
    ~cSC4AdviceSubject() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cRZUnknown::AddRef(); }
    std::uint32_t Release() override { return cRZUnknown::Release(); }

    void SetLocation(float x, float z) override;
    void ValidateLocation();
    float GetLocationX() override { return hasLocation_ ? locationX_ : 0.0f; }
    float GetLocationZ() override { return hasLocation_ ? locationZ_ : 0.0f; }
    bool HasLocation() override { return hasLocation_; }
    bool SetObject(cIGZUnknown* object) override;
    cIGZUnknown* GetObjectA() override { return object_; }
    std::uint32_t GetData1() const override { return data1_; }
    void SetData1(std::uint32_t value) override { data1_ = value; }
    std::uint32_t GetData2() const override { return data2_; }
    void SetData2(std::uint32_t value) override { data2_ = value; }
    bool Clone(cISC4AdviceSubject** out) override;

    bool Write(cIGZOStream& stream) const override;
    bool Read(cIGZIStream& stream) override;
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4AdviceSubject; }

private:
    bool hasLocation_ = false;       // +0x20
    float locationX_ = 0.0f;         // +0x24
    float locationZ_ = 0.0f;         // +0x28
    std::uint32_t data1_ = 0;        // +0x2c
    std::uint32_t data2_ = 0;        // +0x30
    cIGZUnknown* object_ = nullptr;  // +0x38
};

}  // namespace sc4
