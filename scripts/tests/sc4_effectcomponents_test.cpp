#include <cstdio>
#include <vector>

#include "rz/PortMemoryStreams.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/EffectComponents.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct FakeCamera final : ICameraControl {
    int zoomLevel = 2;
    float zoom = -1, rotation = -1;
    int targets = 0;
    cS3DVector3 target;
    int ZoomLevel() override { return zoomLevel; }
    void SetZoom(float z) override { zoom = z; }
    void SetRotation(std::uint32_t r) override { rotation = static_cast<float>(r); }
    void SetViewTargetPosition(const cS3DVector3& p) override { target = p; ++targets; }
};
struct Host final : IEffectsHost {
    FakeCamera camera;
    bool enabled = true, sound = true;
    std::vector<std::uint32_t> notifications, removed;
    struct Event { std::uint32_t id, a; std::uint64_t b, c, d; };
    std::vector<Event> events;
    ICameraControl* Camera() override { return &camera; }
    bool CameraEffectsEnabled() override { return enabled; }
    void AddNotification(cIGZMessageTarget2*, std::uint32_t m) override { notifications.push_back(m); }
    void RemoveNotification(cIGZMessageTarget2*, std::uint32_t m) override { removed.push_back(m); }
    void PostSoundEvent(std::uint32_t e, std::uint32_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d) override { events.push_back({e, a, b, c, d}); }
    bool HasSoundSystem() override { return sound; }
};

template <class T>
T RoundTrip(const T& in) {
    PortMemoryOStream out;
    in.Write(out);
    T result;
    PortMemoryIStream stream(out.bytes().data(), out.bytes().size());
    result.Read(stream);
    return result;
}
}  // namespace

int main() {
    Host host;
    SetEffectsHost(&host);

    // description round trips and defaults
    cSC4CameraDescription cam;
    cam.flags = 0xf; cam.zoom = 5; cam.rotation = 3; cam.radius = 12.5f;
    auto cam2 = RoundTrip(cam);
    CHECK(cam2.flags == 0xf && cam2.zoom == 5 && cam2.rotation == 3 && cam2.radius == 12.5f && cam.Type() == 8);
    cSC4SoundDescription snd;
    CHECK(snd.interval == 0.5f && snd.Type() == 7);
    snd.soundId = 0x1234; snd.duration = 2.0f; snd.flags = 7;
    auto snd2 = RoundTrip(snd);
    CHECK(snd2.soundId == 0x1234 && snd2.duration == 2.0f && snd2.flags == 7 && snd2.interval == 0.5f);
    cSC4AttractorDescription att;
    att.name = "crowd"; att.flag = true;
    auto att2 = RoundTrip(att);
    CHECK(att2.name == "crowd" && att2.flag && att.Type() == 4);
    cSC4SequenceDescription seq;
    seq.items.push_back({{0.0f, 1.5f}, "first"});
    seq.items.push_back({{2.0f, 3.0f}, "second"});
    seq.value = 9;
    auto seq2 = RoundTrip(seq);
    CHECK(seq2.items.size() == 2 && seq2.items[1].name == "second" && seq2.items[1].range.y == 3.0f && seq2.value == 9 && seq.Type() == 6);
    cSC4BrushDescription brush;
    CHECK(brush.id == 7 && brush.f18 == 1.0f && brush.u24 == 0xffffffffu && brush.f38 == 100.0f && brush.Type() == 3);
    brush.f1c = 4.0f; brush.flag = 1; brush.v30 = {5.0f, 6.0f};
    auto brush2 = RoundTrip(brush);
    CHECK(brush2.f1c == 4.0f && brush2.flag == 1 && brush2.v30.y == 6.0f && brush2.f38 == 100.0f);
    cSC4ScrubberDescription scrub;
    CHECK(scrub.u28 == 1 && scrub.f4c == 16.0f && scrub.Type() == 5);
    scrub.u18 = 3; scrub.u38 = 8; scrub.u3c = 9; scrub.v50 = {1.0f, 2.0f};
    auto scrub2 = RoundTrip(scrub);
    CHECK(scrub2.u18 == 3 && scrub2.u38 == 8 && scrub2.u3c == 9 && scrub2.v50.y == 2.0f && scrub2.f4c == 16.0f);

    // instantiation: camera / sound are built in, the rest need a registered factory
    CHECK(seq.Instantiate() == nullptr);
    int made = 0;
    SetEffectComponentFactory(EffectComponentType::Sequence, [&](cISC4ComponentDescription*) { ++made; return nullptr; });
    seq.Instantiate();
    CHECK(made == 1);

    // camera effect
    cam.AddRef();
    auto* effect = new cSC4CameraEffect(&cam);
    effect->AddRef();
    cS3DTransform parent, local;
    parent.flags = 1; parent.translation[0] = 10; parent.translation[1] = 20; parent.translation[2] = 30;
    local.flags = 1; local.translation[0] = 100;
    effect->SetTransforms(parent, local);
    CHECK(effect->Position().x == 110 && effect->Position().y == 20 && effect->Position().z == 30);
    effect->Start(0);
    CHECK(effect->IsStarted() && host.camera.zoom == 5.0f && host.camera.rotation == 3.0f && host.camera.targets == 1);
    CHECK(host.notifications.size() == 1 && host.notifications[0] == kMessageCameraMoved && effect->Following());
    host.camera.targets = 0;
    effect->ApplyEffect(0.1f);
    CHECK(host.camera.targets == 1 && host.camera.target.x == 110);
    auto* moved = new cRZMessage2Standard();
    moved->AddRef();
    moved->SetType(kMessageCameraMoved);
    effect->DoMessage(moved);                      // the user took the camera
    CHECK(!effect->Following());
    host.camera.targets = 0;
    effect->ApplyEffect(0.1f);
    CHECK(host.camera.targets == 0);
    // a ray close to the effect makes it follow again
    const float ray[6] = {110, 20, 28, 0, 0, 1};   // passes within 0 of the effect, radius 12.5
    CHECK(effect->SetFloatParams(0xe, ray, 6) && effect->Following());
    CHECK(!effect->SetFloatParams(3, ray, 6));
    effect->Stop(0);
    CHECK(!effect->IsStarted() && host.removed.size() == 1);
    moved->Release();
    effect->Release();

    // sound effect
    snd.AddRef();
    auto* sound = new cSC4SoundEffect(&snd);
    sound->AddRef();
    cS3DTransform at;
    at.flags = 1; at.translation[0] = 4; at.translation[1] = 5; at.translation[2] = 6;
    sound->SetTransforms(cS3DTransform{}, at);
    sound->Start(0);
    CHECK(sound->IsStarted() && host.events.size() == 1 && host.events[0].id == kSoundEventPlay && host.events[0].a == 0x1234);
    sound->ApplyEffect(0.25f);                      // first move event (interval 0.5 elapsed since 0)
    CHECK(host.events.size() == 2 && host.events[1].id == kSoundEventMove && host.events[1].b == 4 && host.events[1].d == 6);
    sound->ApplyEffect(0.25f);
    CHECK(host.events.size() == 2);                 // next ping at 0.75
    sound->ApplyEffect(0.6f);
    CHECK(host.events.size() == 3);
    sound->ApplyEffect(2.0f);                       // past the 2 s duration: stops itself
    CHECK(!sound->IsStarted() && host.events.back().id == kSoundEventStop);
    sound->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
