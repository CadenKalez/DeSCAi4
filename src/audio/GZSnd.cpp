#include "audio/GZSnd.h"

#include <cstdint>
#include <memory>

#include "audio/AudioServices.h"
#include "audio/platform/AudioPlatform.h"

namespace {
constexpr std::uint32_t kIidDataSourceHolder = 0x09d002d2;
constexpr std::uint32_t kIidCompletionNotification = 0xe9d00a9a;
constexpr std::uint32_t kFadeTickMs = 0x14;
}  // namespace

cGZSnd::cGZSnd() = default;

// ~cGZSnd @ 0x100625770: a playing sound is stopped, a loaded one unloaded.
cGZSnd::~cGZSnd() {
    if (IsPlaying()) Stop();
    if (IsLoaded()) Unload();
}

bool cGZSnd::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSnd) *out = static_cast<cIGZSnd*>(this);
    else if (iid == kIidDataSourceHolder) *out = static_cast<cIGZSndDataSourceHolder*>(this);
    else if (iid == kIidCompletionNotification) *out = static_cast<cIGZSndCompletionNotification*>(this);
    else return false;
    AddRef();
    return true;
}

// cGZSnd::Release() @ 0x100625bc0: at counts below 2 the sound is shut down and destroyed.
std::uint32_t cGZSnd::Release() {
    if (refCount_ < 2) {
        Shutdown();
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

// cGZSnd::Shutdown() @ 0x100625ca0
bool cGZSnd::Shutdown() {
    Stop();
    Unload();
    dataSource_ = nullptr;
    fileSet_ = false;
    return true;
}

std::int32_t cGZSnd::MasterVolume() const {
    cIGZSndSys* system = Audio::SoundSystem();
    return system ? system->GetMasterVolume() : 0x400;
}

// cGZSnd::Play(int, bool, SndDup) @ 0x100625d90
bool cGZSnd::PlayWithVolumeAndLoop(std::int32_t volume, bool loop, SndDup) {
    if (dirtyForReload_) {
        dirtyForReload_ = false;
        Unload();
    }
    if (!Load()) return false;
    SetVolume(volume);
    if (dataSourceMode_) {
        pcm_.Play(loop);
    } else if (fileSet_) {
        file_.Play(loop);
    } else {
        return false;
    }
    return true;
}

bool cGZSnd::IsPlaying() const { return dataSourceMode_ ? pcm_.IsPlaying() : file_.IsPlaying(); }

// cGZSnd::Stop() @ 0x100625f30
bool cGZSnd::Stop() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (IsFading()) StopFadeUpdates();
    if (dataSourceMode_) pcm_.Stop(); else file_.Stop();
    return true;
}

// cGZSnd::Load() @ 0x100626100. File sounds open the file; data-source sounds read the whole
// source into a PCM buffer (opening and closing the source around the read when it was closed).
bool cGZSnd::Load() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (IsLoaded()) return true;
    dirtyForReload_ = false;
    if (!dataSourceMode_) {
        if (!fileSet_) return false;
        return file_.Open(path_.c_str());
    }
    cIGZSndDataSource* source = dataSource_.get();
    if (!source) return false;
    const bool wasOpen = source->IsOpen();
    if (!wasOpen && !source->Open()) return false;
    const GZSndWaveFormat* format = source->GetWaveFormat();
    const std::uint32_t size = source->GetDataSize();
    auto buffer = std::make_unique<std::uint8_t[]>(static_cast<std::size_t>(size) + 0x100);
    const std::int32_t read = source->ReadPart(buffer.get(), static_cast<std::int32_t>(size));
    if (!wasOpen) source->Close();
    // CPCMPlayer::Open takes ownership of the buffer and frees it itself (also on failure). The
    // original deletes it a second time when opening fails; that double free is not reproduced.
    std::uint8_t* raw = buffer.release();
    return pcm_.Open(format->channels, format->bitsPerSample, format->samplesPerSecond, raw, read);
}

// cGZSnd::Unload() @ 0x100626520
bool cGZSnd::Unload() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (IsPlaying()) Stop();
    if (dataSourceMode_) pcm_.Close(); else file_.Close();
    return true;
}

bool cGZSnd::IsLoaded() const { return dataSourceMode_ ? pcm_.IsOpen() : file_.IsOpen(); }

// cGZSnd::Update(int) @ 0x100626740: one step of a running fade.
void cGZSnd::Update(std::int32_t) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (!fading_) return;
    std::int32_t next = volume_ + fadeStep_;
    if ((fadeStep_ < 0 && next <= fadeTarget_) || (fadeStep_ > 0 && fadeTarget_ <= next)) {
        if (next == 0) Kill();
        StopFadeUpdates();
        next = fadeTarget_;
    }
    SetVolume(next);
}

// cGZSnd::SetVolume(int) @ 0x1006268a0: the stored volume is the requested one; the player gets
// it scaled by the sound system's master volume.
bool cGZSnd::SetVolume(std::int32_t volume) {
    if (volume < 0) volume = 0;
    else if (volume > 0x400) volume = 0x400;
    volume_ = volume;
    const float scaled = static_cast<float>((volume * MasterVolume()) / 0x400) / 1024.0f;
    if (dataSourceMode_) pcm_.SetVolume(scaled); else file_.SetVolume(scaled);
    return true;
}

// cGZSnd::FadeVolume @ 0x100626990. `startFade` false or equal endpoints do nothing. The step is
// the per-20ms change; a step of zero applies the target at once. Deviation: durations below
// 20 ms (a division by zero in the original) also apply the target at once.
bool cGZSnd::FadeVolume(std::int32_t fromVolume, std::int32_t toVolume, std::uint32_t durationMs, bool startFade) {
    if (toVolume != fromVolume && startFade) {
        if (IsFading()) return false;
        const auto ticks = static_cast<std::int32_t>(durationMs / kFadeTickMs);
        fadeStep_ = ticks == 0 ? 0 : (toVolume - fromVolume) / ticks;
        if (fadeStep_ == 0) {
            SetVolume(toVolume);
        } else {
            fadeTarget_ = toVolume;
            SetVolume(fromVolume);
            StartFadeUpdates();
        }
    }
    return true;
}

void cGZSnd::StopFadeUpdates() {
    fading_ = false;
    UpdateUpdates();
}

void cGZSnd::StartFadeUpdates() {
    fading_ = true;
    UpdateUpdates();
}

// cGZSnd::UpdateUpdates() @ 0x100627180: starts / stops the 20 ms timer that drives Update().
void cGZSnd::UpdateUpdates() {
    AudioPlatform::IBackend* backend = AudioPlatform::GetBackend();
    if (!timerRunning_) {
        if (fading_) {
            if (timerId_ == 0 && backend) timerId_ = backend->AddTimer(kFadeTickMs, &cGZSnd::TimerProc, this);
            timerRunning_ = true;
        }
    } else if (!fading_) {
        if (timerId_ != 0) {
            if (backend) backend->RemoveTimer(timerId_);
            timerId_ = 0;
        }
        timerRunning_ = false;
    }
}

std::uint32_t cGZSnd::TimerProc(std::uint32_t interval, void* context) {
    static_cast<cGZSnd*>(context)->Update(0);
    return interval;
}

// cGZSnd::SetPan(int) @ 0x100626b00: maps 0..1024 to a balance of -1..1 (PCM sounds only).
bool cGZSnd::SetPan(std::int32_t pan) {
    if (pan < 0) pan = 0;
    else if (pan > 0x400) pan = 0x400;
    if (dataSourceMode_) pcm_.SetBalance((static_cast<float>(pan) / 1024.0f) * 2.0f - 1.0f);
    return true;
}

std::int32_t cGZSnd::GetFrequency() const { return dataSourceMode_ ? static_cast<std::int32_t>(pcm_.GetFrequency()) : 0; }

// cGZSnd::SetFrequency(int) @ 0x100626c20: 1..999 become 1000; values above 100000 are clamped.
bool cGZSnd::SetFrequency(std::int32_t frequency) {
    std::int32_t value = frequency;
    if (frequency == 0 || frequency > 999) {
        if (frequency > 100000) value = 100000;
    } else {
        value = 1000;
    }
    if (dataSourceMode_) pcm_.SetFrequency(static_cast<std::uint32_t>(value));
    return true;
}

std::int32_t cGZSnd::GetOriginalFrequency() const {
    return dataSourceMode_ ? static_cast<std::int32_t>(pcm_.GetOriginalFrequency()) : 0;
}

bool cGZSnd::SetName(const cIGZString& name) {
    name_.assign(name.Data(), name.Strlen());
    return true;
}

bool cGZSnd::GetName(cIGZString& name) const {
    name.AssignFromChars(name_.data(), static_cast<std::uint32_t>(name_.size()));
    return true;
}

bool cGZSnd::SetSoundFileFromString(const cIGZString& path, SndStreamingType streaming) {
    return SetSoundFileFromChars(path.Data(), path.Strlen(), streaming);
}

// cGZSnd::SetSoundFile(char const*, unsigned int, SndStreamingType) @ 0x100626e50
bool cGZSnd::SetSoundFileFromChars(const char* path, std::uint32_t length, SndStreamingType) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    path_.assign(path, length);
    fileSet_ = true;
    dataSourceMode_ = false;
    dirtyForReload_ = true;
    return fileSet_;
}

bool cGZSnd::SetDataSource(cIGZSndDataSource* source, SndStreamingType) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    dataSourceMode_ = true;
    dataSource_ = source;
    dirtyForReload_ = true;
    return true;
}

cIGZSndDataSource* cGZSnd::GetDataSource() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return dataSource_.get();
}
