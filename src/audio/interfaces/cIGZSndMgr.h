// cIGZSndMgr — the sound manager service (service 0x1af85bfb, iid 0x3af85bb7). The interface is
// the secondary base at +0x28 of cGZSndMgr (vtable group 2 @ 0x100ab64b8); slot numbers and byte
// offsets below are those of that group, slots 3..31.
#pragma once
#include <cstdint>

#include "audio/interfaces/cIGZSnd.h"
#include "audio/interfaces/cIGZSndDataSourceCreator.h"
#include "audio/interfaces/cIGZSoundCreator.h"
#include "gz/cGZPersistResourceKey.h"
#include "audio/interfaces/cIGZSndDataSource.h"
#include "gz/cIGZVariant.h"


class cIGZSndMgr : public cIGZUnknown {
public:
    virtual bool GetResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out) = 0;   // [3]  +0x18
    virtual void SetSystemSndVol(std::int32_t volume) = 0;                                          // [4]  +0x20
    virtual std::int32_t GetSystemSndVol() const = 0;                                               // [5]  +0x28
    virtual std::uint32_t SystemSndID(std::int32_t index) = 0;                                      // [6]  +0x30
    virtual bool SetSystemSndID(std::int32_t index, std::uint32_t id) = 0;                          // [7]  +0x38
    virtual bool SetDefaultSndDataSourcePlayer2DClassId(std::uint32_t classId) = 0;                 // [8]  +0x40
    virtual bool GetDefaultSndDataSourcePlayer2DClassId(std::uint32_t& classId) const = 0;          // [9]  +0x48
    virtual bool SetDefaultSndDataSourcePlayer3DClassId(std::uint32_t classId) = 0;                 // [10] +0x50
    virtual bool GetDefaultSndDataSourcePlayer3DClassId(std::uint32_t& classId) const = 0;          // [11] +0x58
    virtual void RegisterSnd(cIGZSnd* snd, std::uint32_t id) = 0;                                   // [12] +0x60
    virtual void UnregisterSndByPointer(cIGZSnd* snd) = 0;                                          // [13] +0x68
    virtual void UnregisterSndById(std::uint32_t id) = 0;                                           // [14] +0x70
    virtual bool RegisterSndCreator(cIGZSoundCreator* creator) = 0;                                 // [15] +0x78
    virtual bool RemoveSndCreator(cIGZSoundCreator* creator) = 0;                                   // [16] +0x80
    // [17] +0x88: creates (or fetches) the sound registered under `id` and returns the interface
    // `iid` of it; the result is AddRef'd.
    virtual bool GetSoundById(std::uint32_t id, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) = 0;
    virtual bool GetSoundByName(cIGZString& name, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) = 0;   // [18] +0x90
    virtual bool SndAutoPlayWithVolume(std::uint32_t id, std::int32_t volume) = 0;                  // [19] +0x98
    virtual bool SndAutoPlay(std::uint32_t id) = 0;                                                 // [20] +0xa0
    virtual bool SystemSndAutoPlayWithVolume(std::uint32_t id, std::int32_t volume) = 0;            // [21] +0xa8
    virtual bool SystemSndAutoPlay(std::uint32_t id) = 0;                                           // [22] +0xb0
    virtual bool AddSndToAutoPlayList(cIGZSnd* snd) = 0;                                            // [23] +0xb8
    virtual bool GetSndDataSource(std::uint32_t id, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) = 0;  // [24] +0xc0
    virtual bool RegisterSndDataSourceCreator(cIGZSndDataSourceCreator* creator, std::uint32_t id) = 0;   // [25] +0xc8
    virtual bool RemoveSndDataSourceCreatorByPointer(cIGZSndDataSourceCreator* creator) = 0;        // [26] +0xd0
    virtual bool RemoveSndDataSourceCreatorById(std::uint32_t id) = 0;                              // [27] +0xd8
    virtual bool GetSndDataSourceCreator(std::uint32_t id, std::uint32_t iid, void** out) = 0;      // [28] +0xe0
    virtual void RegisterSndDataSource(cIGZSndDataSource* source, std::uint32_t id) = 0;            // [29] +0xe8
    virtual void UnregisterSndDataSourceByPointer(cIGZSndDataSource* source) = 0;                   // [30] +0xf0
    virtual void UnregisterSndDataSourceById(std::uint32_t id) = 0;                                 // [31] +0xf8
};

constexpr std::uint32_t kGZSID_cGZSndMgr = 0x1af85bfb;
constexpr std::uint32_t kGZIID_cIGZSndMgr = 0x3af85bb7;
