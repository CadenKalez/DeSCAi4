#include "sc4/MySimBalloonAdvice.h"

#include <cstdio>

#include "sc4/TablePropertyReader.h"

namespace sc4 {

cSC4MySimBalloonAdvice::cSC4MySimBalloonAdvice() = default;

bool cSC4MySimBalloonAdvice::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISC4MySimBalloonAdvice) {
        *out = static_cast<cISC4MySimBalloonAdvice*>(this);
    } else if (iid == kGZIID_cSC4MySimBalloonAdviceSelf) {
        *out = static_cast<cSC4MySimBalloonAdvice*>(this);
    } else {
        return cSC4Advice::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

bool cSC4MySimBalloonAdvice::Init(int adviceId) {
    const bool baseOk = cSC4Advice::Init(adviceId);
    cTablePropertyReader reader(AdviceHost()->Lua(), "advices", adviceId);
    if (!reader.IsValid()) return false;

    cRZString message(reader.GetString("balloon_message"));
    balloonText_.FromScriptText(message.ToChar());

    icons_.clear();
    for (int i = 0; i < kBalloonMaxIcons; ++i) {
        char key[16];
        std::snprintf(key, sizeof key, "balloon_icon%d", i + 1);
        if (!reader.HasField(key)) continue;
        const std::uint32_t instance = reader.GetNumber(key);
        if (instance == 0) continue;
        cSLBuffer icon(false);
        icon.SetResKey(kSLBitmapResourceType, kBalloonIconGroup, instance);
        if (icon.Load() && icon.GetBuffer()) {
            cIGZBuffer* bitmap = icon.GetBuffer();
            bitmap->SetTransparency(bitmap->ConvertRGBValueToNative(0xff, 0, 0xff));   // magenta colour key
            icons_.push_back(icon);
        }
    }

    bool result = false;
    if (baseOk) result = icons_.empty() ? message.Strlen() != 0 : true;

    soundMale_ = soundFemale_ = reader.HasField("balloon_sound") ? static_cast<int>(reader.GetNumber("balloon_sound")) : 0;
    if (reader.HasField("balloon_sound_m")) soundMale_ = static_cast<int>(reader.GetNumber("balloon_sound_m"));
    if (reader.HasField("balloon_sound_f")) soundFemale_ = static_cast<int>(reader.GetNumber("balloon_sound_f"));
    if (reader.HasField("balloon_sound_chance")) soundChance_ = static_cast<int>(reader.GetNumber("balloon_sound_chance"));

    textFields_.emplace_back("balloon_message", &balloonText_);   // refreshed with the other texts
    return result;
}

bool cSC4MySimBalloonAdvice::GetTrigger() const {
    if (CheckStatusFlags(kBalloonStatusBlocked)) return false;
    if (!CheckStatusFlags(kBalloonStatusShown) && !CheckFlags(kAdviceFlagCurrent)) return false;
    return cSC4Advice::GetTrigger();
}

cIGZBuffer* cSC4MySimBalloonAdvice::GetIconBalloon(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= icons_.size()) return nullptr;
    return icons_[static_cast<std::size_t>(index)].GetBuffer();
}

std::uint32_t cSC4MySimBalloonAdvice::GetIconBalloonID(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= icons_.size()) return 0;
    cGZPersistResourceKey key;
    icons_[static_cast<std::size_t>(index)].GetResKey(key);
    return key.instance;
}

void cSC4MySimBalloonAdvice::SetStatusFlags(int flags, bool on) {
    const std::uint32_t mask = static_cast<std::uint32_t>(flags);
    if (on) {
        statusFlags_ |= mask;
        if ((statusFlags_ & 2u) != 0 && CheckFlags(kAdviceFlagTextDirty)) RefreshText();
    } else {
        statusFlags_ &= ~mask;
    }
}

}  // namespace sc4
