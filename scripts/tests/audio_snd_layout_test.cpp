// Verifies that the recovered interface declarations place every virtual at the vtable byte
// offset observed in the original binary (scripts/vtable.py).
#include "audio/interfaces/cIGZSnd.h"
#include "audio/interfaces/cIGZSnd3D.h"
#include "audio/interfaces/cIGZSndCompletionNotification.h"
#include "audio/interfaces/cIGZSndDataSourceHolder.h"
#include "audio/interfaces/cIGZSndRegisterSet.h"
#include "audio/interfaces/cIGZSndUpdateTarget.h"
#include "audio/interfaces/cIGZSndViewUpdateTarget.h"
#include "audio/interfaces/cIHitSem.h"
#include "audio/interfaces/cIHitTrackDataBlock.h"
#include "audio/interfaces/cIFreshCell.h"
#include "audio/interfaces/cIFreshCellPlayer.h"
#include "audio/interfaces/cIFreshDimensionPlayer.h"
#include "audio/interfaces/cIFreshPlayer.h"
#include "audio/interfaces/cIFreshScore.h"
#include "audio/HitChannel.h"
#include "audio/HitControlGroup.h"
#include "audio/HitMan.h"
#include "audio/interfaces/cIHitControlGroup.h"
#include "audio/interfaces/cIHitList.h"
#include "gz/cIGZString.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZSerializable.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Byte offset of a virtual member function inside its vtable (Itanium C++ ABI, both variants).
template <class M>
std::size_t Slot(M m) {
    struct Raw { std::uintptr_t ptr; std::intptr_t adj; } raw;
    static_assert(sizeof(M) == sizeof(Raw));
    std::memcpy(&raw, &m, sizeof raw);
#if defined(__arm__) || defined(__aarch64__)
    assert(raw.adj & 1);               // ARM variant: virtual flag lives in adj
    return raw.ptr;
#else
    assert(raw.ptr & 1);
    return raw.ptr - 1;
#endif
}

#define EXPECT(member, offset) assert(Slot(&member) == (offset))

int main() {
    // cIGZSnd 0x18..0x120
    EXPECT(cIGZSnd::Init, 0x18);            EXPECT(cIGZSnd::Shutdown, 0x20);
    EXPECT(cIGZSnd::PlayWithVolumeAndLoop, 0x28); EXPECT(cIGZSnd::PlayWithScriptArguments, 0x30);
    EXPECT(cIGZSnd::IsPlaying, 0x38);       EXPECT(cIGZSnd::Stop, 0x40);   EXPECT(cIGZSnd::Kill, 0x48);
    EXPECT(cIGZSnd::StopAllInstances, 0x50); EXPECT(cIGZSnd::GetUseCount, 0x58);
    EXPECT(cIGZSnd::GetID, 0x60);           EXPECT(cIGZSnd::SetID, 0x68);
    EXPECT(cIGZSnd::Load, 0x70);            EXPECT(cIGZSnd::Unload, 0x78); EXPECT(cIGZSnd::IsLoaded, 0x80);
    EXPECT(cIGZSnd::Pause, 0x88);           EXPECT(cIGZSnd::Unpause, 0x90); EXPECT(cIGZSnd::IsPaused, 0x98);
    EXPECT(cIGZSnd::GetVolume, 0xa0);       EXPECT(cIGZSnd::SetVolume, 0xa8);
    EXPECT(cIGZSnd::FadeVolume, 0xb0);      EXPECT(cIGZSnd::IsFading, 0xb8);
    EXPECT(cIGZSnd::GetPan, 0xc0);          EXPECT(cIGZSnd::SetPan, 0xc8);
    EXPECT(cIGZSnd::GetFrequency, 0xd0);    EXPECT(cIGZSnd::SetFrequency, 0xd8);
    EXPECT(cIGZSnd::GetOriginalFrequency, 0xe0);
    EXPECT(cIGZSnd::SetEffectsType, 0xe8);  EXPECT(cIGZSnd::SetEffectsLevel, 0xf0);
    EXPECT(cIGZSnd::GetSoundPriority, 0xf8); EXPECT(cIGZSnd::SetSoundPriority, 0x100);
    EXPECT(cIGZSnd::SetName, 0x108);        EXPECT(cIGZSnd::GetName, 0x110);
    EXPECT(cIGZSnd::SetSoundFileFromString, 0x118); EXPECT(cIGZSnd::SetSoundFileFromChars, 0x120);
    // cIGZSnd3D 0x18..0x80
    EXPECT(cIGZSnd3D::AsIGZSnd, 0x18);      EXPECT(cIGZSnd3D::GetPosition, 0x20); EXPECT(cIGZSnd3D::SetPosition, 0x28);
    EXPECT(cIGZSnd3D::GetVelocity, 0x30);   EXPECT(cIGZSnd3D::SetVelocity, 0x38);
    EXPECT(cIGZSnd3D::GetAll, 0x40);        EXPECT(cIGZSnd3D::SetAll, 0x48);   EXPECT(cIGZSnd3D::SetPower, 0x50);
    EXPECT(cIGZSnd3D::GetMinimumDistance, 0x58); EXPECT(cIGZSnd3D::SetMinimumDistance, 0x60);
    EXPECT(cIGZSnd3D::GetMaximumDistance, 0x68); EXPECT(cIGZSnd3D::SetMaximumDistance, 0x70);
    EXPECT(cIGZSnd3D::SetHeadRelative, 0x78);    EXPECT(cIGZSnd3D::GetHeadRelative, 0x80);
    // small interfaces
    EXPECT(cIGZSndUpdateTarget::Update, 0x18);   EXPECT(cIGZSndViewUpdateTarget::UpdateView, 0x18);
    EXPECT(cIGZSndDataSourceHolder::SetDataSource, 0x18); EXPECT(cIGZSndDataSourceHolder::GetDataSource, 0x20);
    EXPECT(cIGZSndCompletionNotification::SetCompletionNotification, 0x18);
    EXPECT(cIGZSndCompletionNotification::StopCompletionNotification, 0x20);
    EXPECT(cIGZSndCompletionNotification::EstimateRemainingPlayTime, 0x28);
    EXPECT(cIGZSndRegisterSet::SetRegisterValue, 0x18); EXPECT(cIGZSndRegisterSet::GetRegisterFloat, 0x40);
    EXPECT(cIHitSem::GetId, 0x18); EXPECT(cIHitSem::GetName, 0x20);
    EXPECT(cIHitSem::UpdateVolPan, 0x28); EXPECT(cIHitSem::ConfigureChannel, 0x30);
    EXPECT(cIHitTrackDataBlock::DataStartAddress, 0x18); EXPECT(cIHitTrackDataBlock::AddEntryPoint, 0x40);
    EXPECT(cIFreshCell::GetSndId, 0x18); EXPECT(cIFreshCell::GetStereoSample, 0xb0);
    EXPECT(cIFreshCell::SetSndId, 0xb8); EXPECT(cIFreshCell::SetStereoSample, 0x150);
    EXPECT(cIFreshDimensionPlayer::AsIFreshPlayer, 0x18); EXPECT(cIFreshDimensionPlayer::SetScoreEnabled, 0x20);
    EXPECT(cIHitControlGroup::AddTrack, 0x18); EXPECT(cIHitControlGroup::AsIGZSnd, 0x20);
    EXPECT(cHitControlGroup::SetVolume, 0x60); EXPECT(cHitControlGroup::Update, 0xf0);
    EXPECT(cHitControlGroup::SetName, 0x1e8); EXPECT(cHitControlGroup::SetSoundFileFromChars, 0x200);
    EXPECT(cHitChannel::AsIGZSndUpdateTarget, 0x138); EXPECT(cHitChannel::SetDataSource, 0x150);
    EXPECT(cHitChannel::GetRegisterValue, 0x168);     EXPECT(cHitChannel::NoteOn, 0x200);
    EXPECT(cHitChannel::UpdatePitch, 0x228);          EXPECT(cHitChannel::UpdateVolPan, 0x230);
    EXPECT(cHitChannel::SetRolloffFactor, 0x240);     EXPECT(cHitChannel::SetTimeMultiplier, 0x270);
    EXPECT(cIHitMan::Init, 0x18);       EXPECT(cIHitMan::LoadHitFile, 0x48);   EXPECT(cIHitMan::GetHitList, 0x80);
    EXPECT(cIHitMan::RegisterSem, 0x98); EXPECT(cIHitMan::Random, 0xd0);       EXPECT(cIHitMan::GetRegisterVal, 0xf8);
    EXPECT(cIHitMan::ControlGroup, 0x118); EXPECT(cIHitMan::DuckMapRemoveAll, 0x150);
    EXPECT(cIHitMan::AddTypeToGroupAssociation, 0x170);
    EXPECT(cHitMan::Pause, 0x80);       EXPECT(cHitMan::ControlGroup, 0x1f8);  EXPECT(cHitMan::DuckMapSetSndobPri, 0x140);
    EXPECT(cHitMan::SetRegisterVal, 0x1b0); EXPECT(cHitMan::UpdateView, 0x130);
    EXPECT(cIFreshScore::GetCell, 0x18);
    EXPECT(cIFreshCellPlayer::GetCell, 0x18); EXPECT(cIFreshCellPlayer::Start, 0x28);
    EXPECT(cIFreshCellPlayer::Kill, 0x40);    EXPECT(cIFreshCellPlayer::Get3DPosZ, 0xb0);
    EXPECT(cIFreshPlayer::InitializeFromLoadedScore, 0x18); EXPECT(cIFreshPlayer::Play, 0x40);
    EXPECT(cIFreshPlayer::FadeVolume, 0x60);     EXPECT(cIFreshPlayer::GetCellPlayer, 0xd0);
    EXPECT(cIFreshPlayer::SetDivision3DPosition, 0x108); EXPECT(cIFreshPlayer::SetExternalSoundSystemInterface, 0x120);
    EXPECT(cIHitList::AddValue, 0x18); EXPECT(cIHitList::Choose, 0x40); EXPECT(cIHitList::RemoveByIndex, 0x80);
    // cIGZString: slots 5..29 (+0x28..+0xe8) from cRZString's vtable
    EXPECT(cIGZString::AssignFromCString, 0x28); EXPECT(cIGZString::ToChar, 0x38); EXPECT(cIGZString::Data, 0x40);
    EXPECT(cIGZString::Strlen, 0x48);            EXPECT(cIGZString::IsEqualToStringPointer, 0x50);
    EXPECT(cIGZString::CompareToString, 0x68);   EXPECT(cIGZString::AssignFromIGZString, 0x78);
    EXPECT(cIGZString::Copy, 0x80);              EXPECT(cIGZString::Resize, 0x88);
    EXPECT(cIGZString::AppendChars, 0x90);       EXPECT(cIGZString::InsertChars, 0xa0);
    EXPECT(cIGZString::ReplaceRangeWithChars, 0xb0); EXPECT(cIGZString::Erase, 0xc0);
    EXPECT(cIGZString::FindChars, 0xc8);         EXPECT(cIGZString::ReverseFindString, 0xe0);
    EXPECT(cIGZString::Sprintf, 0xe8);
    // streams, resources, serializable
    EXPECT(cIGZIStream::Skip, 0x18);   EXPECT(cIGZIStream::GetUint32, 0x48); EXPECT(cIGZIStream::GetVoid, 0x88);
    EXPECT(cIGZIStream::GetError, 0x90); EXPECT(cIGZIStream::GetUserData, 0xa0);
    EXPECT(cIGZOStream::Flush, 0x18);  EXPECT(cIGZOStream::SetUint32, 0x48); EXPECT(cIGZOStream::SetVoid, 0x88);
    EXPECT(cIGZOStream::GetUserData, 0xa0);
    EXPECT(cIHitPersistResource::GetKey, 0x18); EXPECT(cIHitPersistResource::GetRefCount, 0x58);
    EXPECT(cIHitPersistResource::Read, 0x60); EXPECT(cIHitPersistResource::CreatePrivateCopy, 0x70);
    EXPECT(cIGZSerializable::Write, 0x18); EXPECT(cIGZSerializable::Read, 0x20); EXPECT(cIGZSerializable::GetGZCLSID, 0x28);
    std::puts("interface layouts match the binary");
}
