// SimCity 4 audio demo: opens the game's Sound.dat through the recovered persistence layer
// (cGZDBSegmentPackedFile), decodes sounds with the recovered data sources (EA XA ADPCM / RIFF wav)
// and plays them through an AVAudioPlayer backend behind the AudioPlatform seam.
//
//   sc4audio --list [N]                 list the first N sound records
//   sc4audio --play <instance-hex>...   play specific sounds
//   sc4audio --sample N                 play N sounds spread over the file
//   sc4audio --wav out.wav <instance>   decode one sound to a wav file instead of playing
//   options: --dat <path to a package>  (default: the Steam install's GameData/Sound.dat)
#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "audio/AudioServices.h"
#include "audio/SegmentDataSources.h"
#include "audio/platform/AudioPlatform.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/AudioPersistAdapter.h"
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

// AVAudioPlayer-backed implementation of the sound system's platform seam.
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
    double duration() const { return player_.duration; }

private:
    AVAudioPlayer* player_ = nil;
};

class AVBackend final : public AudioPlatform::IBackend {
public:
    AudioPlatform::IVoice* CreatePcmVoice(const std::uint8_t* data, std::int32_t size, std::uint16_t channels,
                                          std::uint32_t rate, std::uint16_t bits) override {
        auto wav = MakeWav(data, static_cast<std::uint32_t>(size), channels, rate, bits);
        NSData* nsdata = [NSData dataWithBytes:wav.data() length:wav.size()];
        auto* voice = new AVVoice(nsdata);
        if (!voice->ok()) { delete voice; return nullptr; }
        return voice;
    }
    AudioPlatform::IVoice* CreateFileVoice(const char* path) override {
        NSData* nsdata = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path]];
        if (!nsdata) return nullptr;
        auto* voice = new AVVoice(nsdata);
        if (!voice->ok()) { delete voice; return nullptr; }
        return voice;
    }
    std::uint32_t AddTimer(std::uint32_t, AudioPlatform::TimerCallback, void*) override { return 0; }
    void RemoveTimer(std::uint32_t) override {}
};

struct Decoded {
    std::vector<std::uint8_t> pcm;
    GZSndWaveFormat format{};
    const char* kind = "?";
};

bool Decode(cIGZSndDataSourceCreator* creator, std::uint32_t instance, Decoded& out) {
    void* raw = nullptr;
    if (!creator->CreateSndDataSource(instance, 0, nullptr, kGZIID_cIGZSndDataSource, &raw)) return false;
    auto* source = static_cast<cIGZSndDataSource*>(raw);
    bool ok = source->Open();
    if (ok) {
        out.format = *source->GetWaveFormat();
        std::uint32_t expected = source->GetDataSize();
        out.pcm.resize(expected);
        std::uint32_t got = 0;
        while (got < expected) {
            std::int32_t n = source->ReadPart(out.pcm.data() + got, static_cast<std::int32_t>(expected - got));
            if (n <= 0) break;
            got += static_cast<std::uint32_t>(n);
        }
        out.pcm.resize(got);
        ok = got != 0;
        source->Close();
    }
    source->Release();
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    @autoreleasepool {
        std::string dat =
            "/Users/fernandojosesampaio/Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/"
            "Sim City 4 Deluxe Edition.app/Contents/GameData/Sound.dat";
        std::vector<std::uint32_t> play;
        int sample = 0, list = 0;
        std::string wavOut;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--dat" && i + 1 < argc) dat = argv[++i];
            else if (a == "--list") list = (i + 1 < argc && argv[i + 1][0] != '-') ? std::atoi(argv[++i]) : 30;
            else if (a == "--sample" && i + 1 < argc) sample = std::atoi(argv[++i]);
            else if (a == "--wav" && i + 1 < argc) wavOut = argv[++i];
            else if (a == "--play") continue;
            else play.push_back(static_cast<std::uint32_t>(std::strtoul(argv[i], nullptr, 16)));
        }

        auto* segment = new cGZDBSegmentPackedFile();
        segment->AddRef();
        segment->Init();
        cRZString path(dat.c_str());
        segment->SetPath(path);
        if (!segment->Open(true, false)) { std::fprintf(stderr, "cannot open %s\n", dat.c_str()); return 1; }

        // collect the sound records and their groups
        auto* keys = new cGZPersistResourceKeyList();
        keys->AddRef();
        cGZPersistResourceKeyFilterByType filter(kGZResourceTypeSndSegment);
        filter.AddRef();
        segment->GetResourceKeyList(keys, &filter);
        std::set<std::uint32_t> groups;
        std::vector<cGZPersistResourceKey> sounds;
        for (std::uint32_t i = 0; i < keys->Size(); ++i) {
            sounds.push_back(*keys->GetKey(i));
            groups.insert(keys->GetKey(i)->group);
        }
        std::printf("%s: %zu sound records in %zu groups\n", dat.c_str(), sounds.size(), groups.size());

        static AVBackend backend;
        AudioPlatform::SetBackend(&backend);
        // the real resource manager owns the package; the sound classes reach it through an adapter
        auto* manager = new cGZPersistResourceManager();
        manager->AddRef();
        manager->RegisterDBSegment(*segment);
        AudioPersistAdapter persist(manager);
        Audio::SetPersistService(&persist);
        auto* creator = cGZSndSegmentDataSourceCreator::Create();
        creator->AddRef();
        for (std::uint32_t g : groups) {
            char text[16];
            std::snprintf(text, sizeof text, "0x%x", g);
            cRZString definition(text);
            creator->AddSndDataSourceDef(g, definition);
        }

        if (list) {
            for (int i = 0; i < list && i < static_cast<int>(sounds.size()); ++i) {
                const auto& k = sounds[static_cast<std::size_t>(i)];
                std::printf("  instance %08x  group %08x  %u bytes\n", k.instance, k.group, segment->GetRecordSize(k));
            }
            return 0;
        }
        if (sample > 0) {
            std::vector<std::uint32_t> chosen;
            std::size_t step = std::max<std::size_t>(1, sounds.size() / static_cast<std::size_t>(sample));
            for (std::size_t i = 0; i < sounds.size() && chosen.size() < static_cast<std::size_t>(sample); i += step)
                if (segment->GetRecordSize(sounds[i]) > 20000) chosen.push_back(sounds[i].instance);
            play.insert(play.end(), chosen.begin(), chosen.end());
        }
        int played = 0;
        for (std::uint32_t instance : play) {
            Decoded d;
            if (!Decode(creator, instance, d)) {
                std::printf("instance %08x: could not decode (UTalk speech needs the SND library codec)\n", instance);
                continue;
            }
            const double seconds = static_cast<double>(d.pcm.size()) /
                                   std::max<std::uint32_t>(1, d.format.averageBytesPerSecond);
            std::printf("instance %08x: %u Hz, %u ch, %u bit, %zu bytes, %.2f s\n", instance, d.format.samplesPerSecond,
                        d.format.channels, d.format.bitsPerSample, d.pcm.size(), seconds);
            if (!wavOut.empty()) {
                auto wav = MakeWav(d.pcm.data(), static_cast<std::uint32_t>(d.pcm.size()), d.format.channels,
                                   d.format.samplesPerSecond, d.format.bitsPerSample);
                FILE* f = std::fopen(wavOut.c_str(), "wb");
                if (f) { std::fwrite(wav.data(), 1, wav.size(), f); std::fclose(f); }
                std::printf("wrote %s\n", wavOut.c_str());
                break;
            }
            AudioPlatform::IVoice* voice = AudioPlatform::GetBackend()->CreatePcmVoice(
                d.pcm.data(), static_cast<std::int32_t>(d.pcm.size()), d.format.channels,
                d.format.samplesPerSecond, d.format.bitsPerSample);
            if (!voice) { std::printf("  backend refused the voice\n"); continue; }
            voice->Play(0);
            ++played;
            NSDate* end = [NSDate dateWithTimeIntervalSinceNow:std::min(seconds + 0.3, 12.0)];
            while (voice->IsPlaying() && [end timeIntervalSinceNow] > 0)
                [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            voice->Stop();
            delete voice;
        }
        std::printf("played %d sound(s)\n", played);
        manager->Shutdown();
        segment->Close();
    }
    return 0;
}
