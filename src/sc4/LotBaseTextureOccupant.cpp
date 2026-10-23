#include "sc4/LotBaseTextureOccupant.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <unordered_map>

#include "rz/cRZDate.h"
#include "sc4/Lot.h"

namespace sc4 {

namespace {

ILotBaseTextureHost* g_host = nullptr;

// (anonymous namespace)::sTextureFrameCountCache @ 0x100b79820: texture id -> animation frame count. Calculated
// once per id for the lifetime of the process (the original builds it with libc++'s __hash_table and rehashes by
// hand; a plain unordered_map is the same container). The cache is deliberately not cleared between cities.
std::unordered_map<std::uint32_t, std::int32_t> sTextureFrameCountCache;

// __GLOBAL__sub_I_SC4LotBaseTextureOccupant.cpp @ 0x1000b68a9..0x1000b6915: seconds since cRZTime::mRefDate
// (0x100b73880 = 9687388), plus the local time of day, XORed with 0x8c09c2d5. An invalid date gives the constant.
std::uint32_t LotBaseTextureRandomSeed() {
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    if (!local) return 0x8c09c2d5u;
    const cRZDate date(static_cast<std::uint32_t>(local->tm_mon + 1), static_cast<std::uint32_t>(local->tm_mday),
                       static_cast<std::uint32_t>(local->tm_year + 1900));
    if (date.JulianDay() == 0) return 0x8c09c2d5u;
    std::uint32_t seed = (date.JulianDay() - 9687388u) * 0x15180u;
    seed += static_cast<std::uint32_t>(local->tm_hour * 3600 + local->tm_min * 60 + local->tm_sec);
    return seed ^ 0x8c09c2d5u;
}

constexpr float kFloatMax = 3.4028234663852886e+38f;    // 0x100a161e0
constexpr float kEmptyBoxMin = -15.9f;                  // 0x100a16a9c
constexpr float kEmptyBoxMax = 15.9f;                   // 0x100a16aa0
constexpr float kBoxInset = 0.1f;                       // 0x100a16204
constexpr float kBoxOutset = -0.1f;                     // 0x100a16aa4
constexpr float kBoxChangeEpsilon = 0.0001f;            // 0x100a16aa8
constexpr float kNegativeIntMinAsFloat = -2147483648.0f;   // 0x100a16a98

}  // namespace

ILotBaseTextureHost* LotBaseTextureHost() { return g_host; }
void SetLotBaseTextureHost(ILotBaseTextureHost* host) { g_host = host; }

cRZRandom cSC4LotBaseTextureOccupant::mRandom(LotBaseTextureRandomSeed());
cRZRandom& cSC4LotBaseTextureOccupant::Random() { return mRandom; }

// --------------------------------------------------------------------------------------------- cSC4LotBaseTextureOccupant

cSC4LotBaseTextureOccupant::cSC4LotBaseTextureOccupant() {   // @ 0x1000b36e0
    type_ = kOccupantTypeLotBaseTexture;   // the original writes 0x497f6d9d to the occupant's +0x48 after the base ctor
}

cSC4LotBaseTextureOccupant::~cSC4LotBaseTextureOccupant() = default;   // @ 0x1000b37ac, 0x1000b382c, 0x1000b3852

bool cSC4LotBaseTextureOccupant::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1000b3cba
    if (iid == kGZIID_cISC4LotBaseTextureOccupant) {
        *out = static_cast<cISC4LotBaseTextureOccupant*>(this);
        AddRef();
        return true;
    }
    return cSC4Occupant::QueryInterface(iid, out);
}

cISC4Occupant* cSC4LotBaseTextureOccupant::AsOccupant() {   // @ 0x1000b5ec6
    return static_cast<cISC4Occupant*>(static_cast<cSC4Occupant*>(this));
}

int cSC4LotBaseTextureOccupant::GetUsage() const { return usage_; }   // @ 0x1000b5ed0 (sign-extended byte)

void cSC4LotBaseTextureOccupant::SetUsage(int usage) {   // @ 0x1000b5ede
    usage_ = static_cast<std::int8_t>(usage);
}

bool cSC4LotBaseTextureOccupant::Shutdown() {   // @ 0x1000b38c4
    if (IsInitialized()) {
        UnloadModels();
        cSC4Occupant::Shutdown();
    }
    return true;   // (sic) never reports the base's result
}

void cSC4LotBaseTextureOccupant::GetPosition(float* xyz) {   // @ 0x1000b3e66
    xyz[0] = (boundsMin_.x + boundsMax_.x) * 0.5f;
    xyz[1] = (boundsMin_.y + boundsMax_.y) * 0.5f;
    xyz[2] = (boundsMin_.z + boundsMax_.z) * 0.5f;
}

void cSC4LotBaseTextureOccupant::GetBoundingBox(float* minXyz, float* maxXyz) {   // @ 0x1000b3efe
    minXyz[0] = boundsMin_.x;
    minXyz[1] = boundsMin_.y;
    minXyz[2] = boundsMin_.z;
    maxXyz[0] = boundsMax_.x;
    maxXyz[1] = boundsMax_.y;
    maxXyz[2] = boundsMax_.z;
}

void cSC4LotBaseTextureOccupant::SetAllFlags(std::uint32_t flags) {   // @ 0x1000b3f4e
    if ((static_cast<std::uint8_t>(~flags_) & flags & kLotBaseTextureFlagShown) != 0) {
        (void)AsPropertyHolder();   // quirk: the original calls it and discards the result
        cISC43DPlaceableObject* placeable = GetPlaceableObject();
        if (placeable) {
            placeable->AddRef();
            LoadModels(false);
            if (placeable->GetRotationsCreatedFor() == 0) {
                placeable->SetRotationsCreatedFor(0xf);
                placeable->SetZoomLevelsCreatedFor(0x1f);
            }
            placeable->Release();
        }
    }
    cSC4Occupant::SetAllFlags(flags);
}

bool cSC4LotBaseTextureOccupant::Write(cIGZOStream& stream) const {   // @ 0x1000b3926
    stream.SetUint16(kLotBaseTextureRecordVersion);
    if (cSC4Occupant::Write(stream)) {
        stream << boundsMin_ << boundsMax_;
        stream.SetSint8(usage_);
        stream.SetUint32(static_cast<std::uint32_t>(specs_.size()));
        for (const LotBaseTextureSpecification& spec : specs_) {
            stream.SetUint32(spec.textureId);
            stream.SetUint8(spec.cellX);
            stream.SetUint8(spec.cellZ);
            stream.SetUint8(spec.rotation);
            stream.SetUint8(spec.flip);
            stream.SetUint8(spec.red);
            stream.SetUint8(spec.green);
            stream.SetUint8(spec.blue);
            stream.SetUint8(spec.alpha);
            stream.SetUint8(spec.combineMode);
            stream.SetUint8(spec.frame);
        }
    }
    // quirk: a failed base write skips the fields but this still answers from the stream error alone
    return !stream.GetError();
}

bool cSC4LotBaseTextureOccupant::Read(cIGZIStream& stream) {   // @ 0x1000b3acc
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != kLotBaseTextureRecordVersion) return false;
    if (cSC4Occupant::Read(stream)) {
        stream >> boundsMin_ >> boundsMax_;
        std::int8_t usage = 0;
        stream.GetSint8(usage);
        usage_ = usage;
        std::uint32_t count = 0;
        stream.GetUint32(count);
        specs_.resize(count);
        for (LotBaseTextureSpecification& spec : specs_) {
            stream.GetUint32(spec.textureId);
            stream.GetUint8(spec.cellX);
            stream.GetUint8(spec.cellZ);
            stream.GetUint8(spec.rotation);
            stream.GetUint8(spec.flip);
            stream.GetUint8(spec.red);
            stream.GetUint8(spec.green);
            stream.GetUint8(spec.blue);
            stream.GetUint8(spec.alpha);
            stream.GetUint8(spec.combineMode);
            stream.GetUint8(spec.frame);
        }
    }
    // quirk: a nested occupant record with a wrong version leaves the fields untouched but still reports success
    return !stream.GetError();
}

std::size_t cSC4LotBaseTextureOccupant::GetCellCoverage(std::vector<SC4PointInt>& cells,
                                                        std::vector<bool>* covered) const {   // @ 0x1000b401a
    const std::size_t count = specs_.size();
    cells.clear();
    if (covered) covered->clear();
    for (const LotBaseTextureSpecification& spec : specs_) {
        cells.push_back(SC4PointInt{spec.cellX, spec.cellZ});
        if (covered) covered->push_back(spec.alpha == 0xff);   // covered = a fully opaque quad
    }
    return count;
}

bool cSC4LotBaseTextureOccupant::IsCellCovered(int x, int z, bool* covered) const {   // @ 0x1000b410e
    if (covered) *covered = false;
    bool found = false;
    for (const LotBaseTextureSpecification& spec : specs_) {
        if (static_cast<std::uint32_t>(x) == spec.cellX) {
            if (static_cast<std::uint32_t>(z) == spec.cellZ) found = true;
            // only an opaque spec finishes the scan early; other specs on the same cell keep the search going
            if (static_cast<std::uint32_t>(z) == spec.cellZ && covered && spec.alpha == 0xff) {
                *covered = true;
                return true;
            }
        }
    }
    return found;
}

void cSC4LotBaseTextureOccupant::CalculateBoundingBox(bool* changed) {   // @ 0x1000b4188
    const cS3DVector3 oldMin = boundsMin_;
    const cS3DVector3 oldMax = boundsMax_;
    ILotBaseTextureHost* host = LotBaseTextureHost();
    if (!host) {   // the original dereferences SL::spLotManager / spTerrain here without checking
        if (changed) *changed = false;
        return;
    }

    // the lot under the first spec's cell decides the altitude (a locked lot sits at its own altitude)
    float lotAltitude = -1.0f;
    if (!specs_.empty()) {
        cSC4Lot* lot = host->LotAtCell(specs_[0].cellX, specs_[0].cellZ);
        if (lot && (lot->GetFlags() & kLotFlagLocked) != 0) lotAltitude = lot->GetAltitude();
    }

    float minX;
    float minY;
    float minZ;
    float maxX;
    float maxY;
    float maxZ;
    if (specs_.empty()) {
        // quirk: the placeholder box of the original is inverted in Y
        minX = kEmptyBoxMin;
        minY = kFloatMax;
        minZ = kEmptyBoxMin;
        maxX = kEmptyBoxMax;
        maxY = kNegativeIntMinAsFloat;
        maxZ = kEmptyBoxMax;
    } else {
        std::int32_t minCellX = 0x7fffffff;
        std::int32_t minCellZ = 0x7fffffff;
        std::int32_t maxCellX = static_cast<std::int32_t>(0x80000000u);
        std::int32_t maxCellZ = static_cast<std::int32_t>(0x80000000u);
        for (const LotBaseTextureSpecification& spec : specs_) {
            minCellX = std::min(minCellX, static_cast<std::int32_t>(spec.cellX));
            minCellZ = std::min(minCellZ, static_cast<std::int32_t>(spec.cellZ));
            maxCellX = std::max(maxCellX, static_cast<std::int32_t>(spec.cellX));
            maxCellZ = std::max(maxCellZ, static_cast<std::int32_t>(spec.cellZ));
        }

        float yMin = kFloatMax;
        float yMax = kNegativeIntMinAsFloat;
        for (const LotBaseTextureSpecification& spec : specs_) {
            if (lotAltitude < 0.0f) {
                // a cell's four terrain vertices: x and x + 1, z and z + 1
                float specMin = yMin;
                float specMax = yMax;
                for (int x = spec.cellX; x <= spec.cellX + 1; ++x) {
                    for (int z = spec.cellZ; z <= spec.cellZ + 1; ++z) {
                        const float height = host->TerrainAltitudeAtVertex(x, z);
                        specMin = std::min(specMin, height);
                        specMax = std::max(specMax, height);
                    }
                }
                yMin = specMin;
                // a flat cell (or no sample at all) gets a 0.1-thick box
                yMax = (specMin >= specMax) ? specMin + kBoxInset : specMax;
            } else {
                yMin = lotAltitude;
                yMax = lotAltitude + kBoxInset;
            }
        }

        minX = static_cast<float>(minCellX << 4) + kBoxInset;
        minY = yMin;
        minZ = static_cast<float>(minCellZ << 4) + kBoxInset;
        maxX = static_cast<float>(maxCellX * 0x10 + 0x10) + kBoxOutset;
        maxY = yMax;
        maxZ = static_cast<float>(maxCellZ * 0x10 + 0x10) + kBoxOutset;
    }

    boundsMin_ = cS3DVector3{minX, minY, minZ};
    boundsMax_ = cS3DVector3{maxX, maxY, maxZ};

    if (changed) {
        *changed = false;
        if (std::fabs(oldMin.x - boundsMin_.x) >= kBoxChangeEpsilon ||
            std::fabs(oldMin.y - boundsMin_.y) >= kBoxChangeEpsilon ||
            std::fabs(oldMin.z - boundsMin_.z) >= kBoxChangeEpsilon ||
            std::fabs(oldMax.x - boundsMax_.x) >= kBoxChangeEpsilon ||
            std::fabs(oldMax.y - boundsMax_.y) >= kBoxChangeEpsilon ||
            std::fabs(oldMax.z - boundsMax_.z) >= kBoxChangeEpsilon) {
            *changed = true;
        }
    }
}

void cSC4LotBaseTextureOccupant::SetTextureSpecification(const LotBaseTextureSpecification* specs,
                                                         std::uint32_t count, bool* changed) {   // @ 0x1000b4594
    if (count == 0) return;   // quirk: the bounding box is not recomputed for an empty list

    // the caller's array is copied first: the frames are stored back into the copy
    std::vector<LotBaseTextureSpecification> incoming(specs, specs + count);
    ILotBaseTextureHost* host = LotBaseTextureHost();
    for (LotBaseTextureSpecification& spec : incoming) {
        if (spec.frame != 0xff) continue;   // a concrete frame (loaded from a save) is kept
        const std::uint32_t textureId = spec.textureId;
        std::int32_t frames;
        const auto cached = sTextureFrameCountCache.find(textureId);
        if (cached != sTextureFrameCountCache.end()) {
            frames = cached->second;
        } else {
            frames = 1;   // no binding: a single frame
            ILotBaseTextureBinding* binding = nullptr;
            if (host) {
                binding = host->CreateTextureBinding(textureId + 4);
                if (!binding) binding = host->CreateTextureBinding(textureId);
            }
            if (binding) {
                frames = static_cast<std::int32_t>(binding->FrameCount());
                binding->Release();
            }
            sTextureFrameCountCache.emplace(textureId, frames);
        }
        // quirk: fewer than two frames always uses frame 0, and the generator is not advanced
        spec.frame = static_cast<std::uint8_t>(
            frames < 2 ? 0 : Random().RandomUint32Uniform(static_cast<std::uint32_t>(frames)));
    }

    const std::size_t existingCount = specs_.size();
    if (existingCount == 0) {
        specs_.insert(specs_.end(), incoming.begin(), incoming.end());
    } else {
        bool same = existingCount == count;
        if (same) {
            // one-to-one match by cell: every new spec must take an unused old spec of the same cell
            std::vector<bool> used(count, false);
            for (std::uint32_t i = 0; i < count && same; ++i) {
                std::uint32_t j = 0;
                for (; j < count; ++j) {
                    if (!used[j] && specs_[j].cellX == incoming[i].cellX && specs_[j].cellZ == incoming[i].cellZ) {
                        specs_[j] = incoming[i];
                        used[j] = true;
                        break;
                    }
                }
                if (j == count) same = false;
            }
        }
        if (same) {
            UpdateExistingModels(true);
        } else {
            UnloadModels();
            specs_.clear();
            specs_.insert(specs_.end(), incoming.begin(), incoming.end());
            // quirk: without a 3D view set the models are never reloaded here
            ILotBaseTextureViewSet* view = host ? host->AcquireViewSet() : nullptr;
            if (view) {
                LoadModels(view->ContainsOccupant(AsOccupant()));
                view->Release();
            }
        }
    }

    CalculateBoundingBox(changed);
}

void cSC4LotBaseTextureOccupant::UpdateExistingModels(bool changed) {   // @ 0x1000b4bd4
    (void)AsPropertyHolder();   // quirk: the original calls it and discards the result
    cISC43DPlaceableObject* placeable = GetPlaceableObject();
    if (!placeable) return;
    placeable->AddRef();
    const int count = placeable->GetModelCount();
    if (count > 0) {
        std::vector<cIS3DModelInstance*> models(static_cast<std::size_t>(count), nullptr);
        std::uint32_t modelCount = static_cast<std::uint32_t>(count);
        placeable->GetModels(models.data(), modelCount);
        ILotBaseTextureHost* host = LotBaseTextureHost();
        const int zoom = host ? host->ViewZoom() : 0;
        for (std::uint32_t i = 0; i < modelCount; ++i) {
            cIS3DModelInstance* instance = models[i];
            if (!instance) continue;
            ILotCellTextureModel* model = nullptr;
            if (!instance->QueryInterface(kGZIID_cISC4CellTextureModel, reinterpret_cast<void**>(&model)) || !model) {
                continue;
            }
            // the original indexes the spec array with the model index and does not bounds-check it; the two are
            // created one-to-one, so a longer model list would read past the end there.
            if (i >= specs_.size()) {
                model->Release();
                break;
            }
            model->UpdateTextures(zoom, specs_[i]);
            model->SetupVertices(true, true, nullptr, nullptr);
            if ((flags_ & kLotBaseTextureFlagShown) != 0) {
                ILotBaseTextureViewSet* view = host ? host->AcquireViewSet() : nullptr;
                const bool inView = (view == nullptr) || view->ContainsOccupant(AsOccupant());
                if (host) {
                    if (inView) {
                        if (changed) host->RendererChangeModel(instance, 2);
                        else host->RendererMoveModel(instance);
                    } else {
                        host->RendererRemoveModel(instance);
                    }
                }
                if (view) view->Release();
            }
            model->Release();
        }
    }
    placeable->Release();
}

void cSC4LotBaseTextureOccupant::UpdateVertexAltitudes(bool allowUpdate, bool force,
                                                       bool* changed) {   // @ 0x1000b4f40
    bool boxChanged = false;
    CalculateBoundingBox(&boxChanged);
    if (force || (boxChanged && allowUpdate)) {
        if ((flags_ & kLotBaseTextureFlagManaged) != 0) {
            ILotBaseTextureHost* host = LotBaseTextureHost();
            if (host) host->MoveOccupant(AsOccupant());   // the original writes through SL::spOccupantManager unchecked
        }
        UpdateExistingModels(false);
    }
    if (changed) *changed = boxChanged;
}

void cSC4LotBaseTextureOccupant::LoadModels(bool setVisibility) {   // @ 0x1000b4fb8
    if (specs_.empty()) return;
    cISC43DPlaceableObject* placeable = GetOrCreatePlaceableObject();
    if (!placeable) return;   // the original dereferences the placeable without a null check
    placeable->AddRef();
    ILotBaseTextureHost* host = LotBaseTextureHost();
    if (placeable->GetModelCount() == 0) {
        for (const LotBaseTextureSpecification& spec : specs_) {
            ILotCellTextureModel* model = host ? host->CreateCellTextureModel(spec) : nullptr;
            if (!model) continue;   // the original would have faulted inside cSC4CellTextureModel::Init
            placeable->AddModel(model->ModelInstance());
            model->Release();
        }
    }
    if (setVisibility) placeable->SetVisibility((flags_ & kLotBaseTextureFlagShown) != 0);
    placeable->Release();
}

void cSC4LotBaseTextureOccupant::UnloadModels() {   // @ 0x1000b52a8
    cISC43DPlaceableObject* placeable = GetPlaceableObject();
    if (!placeable) return;
    placeable->AddRef();
    placeable->RemoveAll();
    placeable->Release();
}

}  // namespace sc4
