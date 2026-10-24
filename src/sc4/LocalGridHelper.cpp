// cSC4LocalGridHelper — see the header for the decoded object, the seams and the quirks. The addresses in
// the comments are the original's (Sim City 4 Deluxe Editionsub 1.2.1, x86_64).
#include "sc4/LocalGridHelper.h"

#include <cmath>

namespace sc4 {
namespace {

ILocalGridHost* g_localGridHost = nullptr;

// 0x100a16904: the scale multiplier (16.0f) and 0x100a16908: its reciprocal as a double (0.0625). The
// original uses the double form only for the snap and the float form for the scale.
constexpr double kSnapFraction = 0.0625;   // 1 / 16 of a cell
constexpr float kScaleCells = 16.0f;

}  // namespace

ILocalGridHost* LocalGridHost() { return g_localGridHost; }
void SetLocalGridHost(ILocalGridHost* host) { g_localGridHost = host; }

// @ 0x1000a98b8
cSC4LocalGridHelper::~cSC4LocalGridHelper() {
    // The destructor releases the effect without stopping it first (0x1000a98c8 jumps straight to the
    // Release slot); a started effect keeps running after its helper is gone.
    if (effect_) effect_->Release();
}

// @ 0x1000a98ce
void cSC4LocalGridHelper::Init() {
    if (effect_ != nullptr) return;                     // 0x1000a98d1: create only once
    ILocalGridHost* host = LocalGridHost();
    if (!host) return;
    ILocalGridEffects* effects = host->Effects();
    if (!effects) return;
    effect_ = effects->Create("local_grid");            // SL::spEffectsManager vslot 0x30
}

// @ 0x1000a98f8
void cSC4LocalGridHelper::Shutdown() {
    if (effect_ == nullptr) return;
    ILocalGridEffect* effect = effect_;
    if (effect->IsStarted()) effect->Stop(0);           // 0x1000a9909/0x1000a991b
    effect_ = nullptr;                                  // 0x1000a9926: null before the release
    effect->Release();                                  // vslot 0x10
}

// @ 0x1000a9940
void cSC4LocalGridHelper::SetPosition(const cS3DVector3& position) {
    if (effect_ == nullptr) return;                     // 0x1000a9955
    ILocalGridHost* host = LocalGridHost();
    ILocalGridTerrain* terrain = host ? host->Terrain() : nullptr;
    if (!terrain) return;                               // the original dereferences SL::spTerrain
    // 0x1000a998d..0x1000a99e2 (x) and 0x1000a99eb..0x1000a9a44 (z): floorsd of position / cell, times
    // cell, plus a sixteenth of a cell. Position.y is copied through bit for bit (0x1000a99e7). The
    // original calls the virtual CellSize five times (division and fraction for x and z, then the scale);
    // the calls are kept separate because a different terrain answer per call would change the result.
    const float cellX = terrain->CellSize();            // vslot 0x30 @ 0x1000a999d
    double snappedX = std::floor(static_cast<double>(position.x) / cellX) * cellX;
    snappedX += static_cast<double>(terrain->CellSize()) * kSnapFraction;   // @ 0x1000a99ca
    const float cellZ = terrain->CellSize();            // @ 0x1000a99ff
    double snappedZ = std::floor(static_cast<double>(position.z) / cellZ) * cellZ;
    snappedZ += static_cast<double>(terrain->CellSize()) * kSnapFraction;   // @ 0x1000a9a2c
    const float scale = terrain->CellSize() * kScaleCells;                  // @ 0x1000a9a4f, mulss 16.0f

    cS3DTransform transform;
    transform.changeCounter = 2;                        // 0x1000a993a: movb $0x2
    // 0x1000a9a5f..0x1000a9a8a: ucomiss + setnp/sete; a NaN scale therefore takes the "3" branch.
    transform.flags = scale == 1.0f ? 1 : 3;
    transform.rotation[0][0] = 1.0f;                    // identity, written float by float at 0x1000a995f
    transform.rotation[0][1] = 0.0f;
    transform.rotation[0][2] = 0.0f;
    transform.rotation[1][0] = 0.0f;
    transform.rotation[1][1] = 1.0f;
    transform.rotation[1][2] = 0.0f;
    transform.rotation[2][0] = 0.0f;
    transform.rotation[2][1] = 0.0f;
    transform.rotation[2][2] = 1.0f;
    transform.translation[0] = static_cast<float>(snappedX);
    transform.translation[1] = position.y;
    transform.translation[2] = static_cast<float>(snappedZ);
    transform.scale = scale;
    effect_->SetEffectTransform(transform);             // vslot 0x40
}

// @ 0x1000a9aac
void cSC4LocalGridHelper::ShowEffect(bool show) {
    if (effect_ == nullptr) return;
    if (show) {
        if (effect_->IsStarted()) return;               // 0x1000a9ac2
        effect_->Start(0);                              // vslot 0x18
    } else {
        if (!effect_->IsStarted()) return;              // 0x1000a9ae0
        effect_->Stop(0);                               // vslot 0x20
    }
}

}  // namespace sc4
