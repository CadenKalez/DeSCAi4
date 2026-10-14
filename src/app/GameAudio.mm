#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cstring>
#include <set>

#include "app/GameAudio.h"
#include "audio/AudioServices.h"
#include "audio/SegmentDataSources.h"
#include "audio/platform/AudioPlatform.h"
#include "gz/AudioPersistAdapter.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZPersistResourceKeyList.h"
#include "gz/cGZPersistResourceManager.h"

namespace {

std::vector<std::uint8_t> MakeWav(const std::uint8_t* pcm, std::uint32_t size, std::uint16_t channels,
                                  std::uint32_t rate, std::uint16_t bits) {
    std::vector<std::uint8_t> w(44 + size);
    auto put32 = [&](std::size_t at, std::uint32_t v) { std::memcpy(&w[at], &v, 4); };
    auto put16 = [&](std::size_t at, std::uint16_t v) { std::memcpy(&w[at], &v, 2); };
    std::memcpy(&w[0], "RIFF", 4); put32(4, 36 + size); std::memcpy(&w[8], "WAVEfmt ", 8);
    put32(16, 16); put16(20, 1); put16(22, channels); put32(24, rate);
    put32(28, rate * channels * bits / 8); put16(32, static_cast<std::uint16_t>(channels * bits / 8)); put16(34, bits);
    std::memcpy(&w[36], "data", 4); put32(40, size);
    std::memcpy(&w[44], pcm, size);
    return w;
}

class AVVoice final : public AudioPlatform::IVoice {
public:
    explicit AVVoice(NSData* data) {
        NSError* err = nil;
        player_ = [[AVAudioPlayer alloc] initWithData:data error:&err];
        if (player_) [player_ prepareToPlay];
    }
    bool ok() const { return player_ != nil; }
    void Play(std::int32_t loops) override { player_.numberOfLoops = loops; [player_ play]; }
    void Stop() override { [player_ stop]; }
    bool IsPlaying() const override { return player_.playing; }
    void SetVolume(float v) override { player_.volume = v; }
    void SetPan(float p) override { player_.pan = p; }

private:
    AVAudioPlayer* player_ = nil;
};

class AVBackend final : public AudioPlatform::IBackend {
public:
    AudioPlatform::IVoice* CreatePcmVoice(const std::uint8_t* data, std::int32_t size, std::uint16_t channels,
                                          std::uint32_t rate, std::uint16_t bits) override {
        auto wav = MakeWav(data, static_cast<std::uint32_t>(size), channels, rate, bits);
        auto* voice = new AVVoice([NSData dataWithBytes:wav.data() length:wav.size()]);
        if (!voice->ok()) { delete voice; return nullptr; }
        return voice;
    }
    AudioPlatform::IVoice* CreateFileVoice(const char*) override { return nullptr; }
    std::uint32_t AddTimer(std::uint32_t, AudioPlatform::TimerCallback, void*) override { return 0; }
    void RemoveTimer(std::uint32_t) override {}
};

}  // namespace

struct GameAudio::Impl {
    cGZDBSegmentPackedFile* segment = nullptr;
    cGZPersistResourceManager* manager = nullptr;
    AudioPersistAdapter* persist = nullptr;
    cIGZSndDataSourceCreator* creator = nullptr;
    AVBackend backend;
    AudioPlatform::IVoice* ambient = nullptr;
    AudioPlatform::IVoice* click = nullptr;

    bool Decode(std::uint32_t instance, std::vector<std::uint8_t>& pcm, GZSndWaveFormat& fmt) {
        void* raw = nullptr;
        if (!creator->CreateSndDataSource(instance, 0, nullptr, kGZIID_cIGZSndDataSource, &raw)) return false;
        auto* source = static_cast<cIGZSndDataSource*>(raw);
        bool ok = source->Open();
        if (ok) {
            fmt = *source->GetWaveFormat();
            const std::uint32_t expected = source->GetDataSize();
            pcm.resize(expected);
            std::uint32_t got = 0;
            while (got < expected) {
                const std::int32_t n = source->ReadPart(pcm.data() + got, static_cast<std::int32_t>(expected - got));
                if (n <= 0) break;
                got += static_cast<std::uint32_t>(n);
            }
            pcm.resize(got);
            ok = got != 0;
            source->Close();
        }
        source->Release();
        return ok;
    }
    AudioPlatform::IVoice* MakeVoice(std::uint32_t instance, double* seconds = nullptr) {
        std::vector<std::uint8_t> pcm;
        GZSndWaveFormat fmt{};
        if (!Decode(instance, pcm, fmt)) return nullptr;
        if (seconds) *seconds = double(pcm.size()) / std::max<std::uint32_t>(1, fmt.averageBytesPerSecond);
        return backend.CreatePcmVoice(pcm.data(), static_cast<std::int32_t>(pcm.size()), fmt.channels,
                                      fmt.samplesPerSecond, fmt.bitsPerSample);
    }
};

GameAudio::~GameAudio() {
    if (impl_) {
        delete impl_->ambient;
        delete impl_->click;
        if (impl_->manager) impl_->manager->Shutdown();
        if (impl_->segment) impl_->segment->Close();
        delete impl_;
    }
}

bool GameAudio::Open(const std::string& soundDat, std::string& error) {
    impl_ = new Impl();
    impl_->segment = new cGZDBSegmentPackedFile();
    impl_->segment->AddRef();
    impl_->segment->Init();
    cRZString path(soundDat.c_str());
    impl_->segment->SetPath(path);
    if (!impl_->segment->Open(true, false)) { error = "cannot open " + soundDat; return false; }
    auto* keys = new cGZPersistResourceKeyList();
    keys->AddRef();
    cGZPersistResourceKeyFilterByType filter(kGZResourceTypeSndSegment);
    filter.AddRef();
    impl_->segment->GetResourceKeyList(keys, &filter);
    std::set<std::uint32_t> groups;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> bySize;   // instance, bytes
    for (std::uint32_t i = 0; i < keys->Size(); ++i) {
        const auto* k = keys->GetKey(i);
        groups.insert(k->group);
        const std::uint32_t size = impl_->segment->GetRecordSize(*k);
        sounds_.push_back(k->instance);
        bySize.push_back({k->instance, size});
    }
    AudioPlatform::SetBackend(&impl_->backend);
    impl_->manager = new cGZPersistResourceManager();
    impl_->manager->AddRef();
    impl_->manager->RegisterDBSegment(*impl_->segment);
    impl_->persist = new AudioPersistAdapter(impl_->manager);
    Audio::SetPersistService(impl_->persist);
    impl_->creator = cGZSndSegmentDataSourceCreator::Create();
    impl_->creator->AddRef();
    for (std::uint32_t g : groups) {
        char text[16];
        std::snprintf(text, sizeof text, "0x%x", g);
        cRZString def(text);
        impl_->creator->AddSndDataSourceDef(g, def);
    }
    // long sounds make the ambient bed, short ones are the UI clicks; spread the picks over the file
    const std::size_t step = std::max<std::size_t>(1, bySize.size() / 40);
    for (std::size_t i = 0; i < bySize.size(); i += step) {
        if (bySize[i].second > 60000) ambient_.push_back(bySize[i].first);
        else if (bySize[i].second > 1500 && bySize[i].second < 12000) clicks_.push_back(bySize[i].first);
    }
    if (ambient_.empty())
        for (std::size_t i = 0; i < bySize.size() && ambient_.size() < 16; i += step)
            if (bySize[i].second > 20000) ambient_.push_back(bySize[i].first);
    return true;
}

void GameAudio::SetMuted(bool muted) {
    muted_ = muted;
    if (impl_ && impl_->ambient) impl_->ambient->SetVolume(muted ? 0.0f : 0.6f);
}

void GameAudio::NextAmbient() {
    if (!impl_ || ambient_.empty()) return;
    for (std::size_t tries = 0; tries < ambient_.size(); ++tries) {
        const std::uint32_t instance = ambient_[nextAmbient_++ % ambient_.size()];
        double seconds = 0;
        AudioPlatform::IVoice* voice = impl_->MakeVoice(instance, &seconds);
        if (!voice) continue;
        if (impl_->ambient) { impl_->ambient->Stop(); delete impl_->ambient; }
        impl_->ambient = voice;
        voice->SetVolume(muted_ ? 0.0f : 0.6f);
        voice->Play(0);
        char name[64];
        std::snprintf(name, sizeof name, "Sound.dat %08x (%.0fs)", instance, seconds);
        current_ = name;
        return;
    }
}

void GameAudio::PlayClick() {
    if (!impl_ || clicks_.empty() || muted_) return;
    for (std::size_t tries = 0; tries < clicks_.size(); ++tries) {
        AudioPlatform::IVoice* voice = impl_->MakeVoice(clicks_[nextClick_++ % clicks_.size()]);
        if (!voice) continue;
        if (impl_->click) { impl_->click->Stop(); delete impl_->click; }
        impl_->click = voice;
        voice->SetVolume(0.8f);
        voice->Play(0);
        return;
    }
}

void GameAudio::Tick() {
    if (impl_ && impl_->ambient && !impl_->ambient->IsPlaying() && !muted_) NextAmbient();
}
