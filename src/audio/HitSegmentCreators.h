// cHitSegmentTrackCreator and cHitSegmentDataSourceChannelCreator — sound creators the Hit
// engine registers with the sound manager: the first builds a track player for a sound id from a
// persisted track definition, the second a channel playing a sound data source. Recovered from
// Sim City 4 Deluxe Editionsub 1.2.1 (track creator vtable 0x100b09ae8, ctor @ 0x10043b468;
// channel creator vtable 0x100b09c48, ctor @ 0x10043ba86).
//
// Bases/offsets: cIGZSoundCreator +0 (id 0x3af9866f), the sound-definition registrar +8
// (id 0x1b6c8b18; QueryInterface(1) returns this one), the track-definition parser registrar
// +0x10 (id 0x5b8d131c). Release destroys at count 1 (count set to 0 first).
#pragma once
#include <cstdint>
#include <list>

#include "audio/interfaces/cIGZSoundCreator.h"
#include "audio/interfaces/cIGZSndMgr.h"
#include "gz/cIGZPersistRecord.h"
#include "rz/cRZAutoRefCount.h"

// A parser for track definitions; only its QueryInterface id (0x7b8c0324) is used here.
class cIGZSndDefParser : public cIGZUnknown {};
constexpr std::uint32_t kGZIID_cIGZSndDefParserBase = 0x7b8c0324;

class cIGZSndDefRegistrar : public cIGZUnknown {
public:
    // [3] +0x18: remembers the sound definition group `group` (the name and variant are unused).
    virtual bool RegisterSoundDef(std::uint32_t group, const cIGZString& name, std::uint32_t arg, const cIGZVariant* variant) = 0;
};
constexpr std::uint32_t kGZIID_cIGZSndDefRegistrar = 0x1b6c8b18;

class cIGZSndTrackDefParserRegistrar : public cIGZUnknown {
public:
    virtual bool RegisterTrackDefParser(cIGZSndDefParser* parser) = 0;      // [3] +0x18
};
constexpr std::uint32_t kGZIID_cIGZSndTrackDefParserRegistrar = 0x5b8d131c;

class cHitSegmentTrackCreator final : public cIGZSoundCreator,
                                      public cIGZSndDefRegistrar,
                                      public cIGZSndTrackDefParserRegistrar {
public:
    static constexpr std::uint32_t kTrackResourceType = 0x0b8d821a;
    static constexpr std::uint32_t kAlternateTrackResourceType = 0x0b9eb87e;

    cHitSegmentTrackCreator();                                          // @ 0x10043b468
    ~cHitSegmentTrackCreator() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;        // @ 0x10043b588
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override;

    // @ 0x10043b602: searches the registered groups for a track resource (type 0x0b8d821a, then
    // 0x0b9eb87e) of the sound id, makes a track player from the Hit manager, hands it the
    // definition, resets it and returns it as `iid`.
    bool CreateSound(std::uint32_t id, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) override;
    bool RegisterSoundDef(std::uint32_t group, const cIGZString& name, std::uint32_t arg, const cIGZVariant* variant) override;   // @ 0x10043b862
    bool RegisterTrackDefParser(cIGZSndDefParser* parser) override;     // @ 0x10043b8a8

    const std::list<std::uint32_t>& groups() const { return groups_; }

private:
    std::int32_t refCount_ = 0;                                         // +0x18
    cRZAutoRefCount<cIGZPersistRecordService> persist_;                 // +0x20
    std::list<std::uint32_t> groups_;                                   // +0x28
    cRZAutoRefCount<cIGZUnknown> parser_;                               // +0x40
};

class cHitSegmentDataSourceChannelCreator final : public cIGZSoundCreator,
                                                  public cIGZSndDefRegistrar,
                                                  public cIGZSndTrackDefParserRegistrar {
public:
    static constexpr std::uint32_t kChannelClassId = 0x3b1ea5be;
    static constexpr std::uint32_t kChannelDefaultSemRegister = 0xff4c97be;
    static constexpr std::uint32_t kChannelDefaultSemValue = 0xcb954e2c;

    cHitSegmentDataSourceChannelCreator();                              // @ 0x10043ba86
    ~cHitSegmentDataSourceChannelCreator() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;        // @ 0x10043bc30
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override;

    // @ 0x10043bcaa: gets the sound data source from the sound manager, wraps it in a new channel
    // (streaming type 2), sets register 0xff4c97be to 0xcb954e2c and returns the channel as `iid`.
    bool CreateSound(std::uint32_t id, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) override;
    bool RegisterSoundDef(std::uint32_t group, const cIGZString& name, std::uint32_t arg, const cIGZVariant* variant) override;   // @ 0x10043be24
    bool RegisterTrackDefParser(cIGZSndDefParser* parser) override;     // @ 0x10043be6a

private:
    std::int32_t refCount_ = 0;                                         // +0x18
    std::list<std::uint32_t> groups_;                                   // +0x20
    cRZAutoRefCount<cIGZUnknown> parser_;                               // +0x30
    cRZAutoRefCount<cIGZPersistRecordService> persist_;                 // +0x38
    cRZAutoRefCount<cIGZSndMgr> soundManager_;                          // +0x48
};
