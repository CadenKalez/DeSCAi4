// cS3DMaterialManager (0x100569e74..0x10056a0a4, iid 0xfac0e21a, a cRZSystemService), the cS3DMaterial record it builds
// and cS3DFSHTexture (0x10046611c..0x1004679a, a persisted FSH container, iid 0x1ab50c3f).
//
// The manager only implements CreateMaterial (copying a resource material into a runtime one, 2 texture layers); the
// variant / instance factories and the statistics are stubs in the shipped build ("Nothing to report").
// Quirk kept: CreateMaterial trusts the layer count (more than 2 overruns the original's 0x50-byte record; here at most 2 are copied).
#pragma once
#include <cstdint>

#include "gz/cGZPersistResourceManager.h"
#include "rz/cRZSystemService.h"
#include "s3d/FSH.h"

namespace s3d {

constexpr std::uint32_t kGZIID_cIS3DMaterialManager = 0xfac0e21a;
constexpr std::uint32_t kGZIID_cIS3DFSHTexture = 0x1ab50c3f;

// A layer of the resource record (0x38 bytes).
struct S3DResMaterialLayer {
    std::uint32_t textureId;              // +0
    std::uint8_t other[0x30];             // +4 (wrap / filter data the manager does not read)
    std::uint8_t samplerBytes[4];         // +0x34
};
// cS3DResMaterial: flags and byte parameters, the layer count and up to two layers.
struct cS3DResMaterial {
    std::uint16_t flags;                  // +0
    std::uint8_t pad2[2];
    std::uint8_t field4, field5, field6, field7, pad8, field9;   // +4..+9
    std::uint8_t pad_a[6];
    std::uint32_t layerCount;             // +0x10
    std::uint32_t pad14;
    S3DResMaterialLayer layers[2];        // +0x18
};
static_assert(sizeof(S3DResMaterialLayer) == 0x38, "layer stride");

struct S3DMaterialLayer {                 // 0x20 bytes at +0x10 + 0x20 * i
    std::uint32_t textureId = 0;          // +0
    std::uint64_t texture = 0;            // +8 resolved texture (null until bound)
    std::uint8_t sampler[4] = {3, 3, 1, 1};   // +0x10 (0x01010303)
    std::uint8_t used = 1;                // +0x14 (0xff marks an unused layer)
    std::uint8_t pad15 = 0;
    std::uint8_t pad16 = 0;               // +0x16
    float scale = 1.0f;                   // +0x18
};
// cS3DMaterial (0x50 bytes).
struct cS3DMaterial {
    std::uint8_t state = 0;               // +0 (1 once built)
    std::int16_t refCount = 0;            // +2
    std::uint16_t flags = 0;              // +4
    std::uint8_t b6 = 0, b7 = 0, b8 = 0, b9 = 0, b10 = 0, layerCount = 0;   // +6..+0xb
    S3DMaterialLayer layers[2];           // +0x10
    cS3DMaterial() = default;             // 0x1000d979e
};

class cS3DMaterialManager : public cRZSystemService {
public:
    cS3DMaterialManager(std::uint32_t serviceId, std::int32_t priority) : cRZSystemService(serviceId, priority) {}
    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0xfac0e21a -> this, else the service base
    // Builds a material from `resource`; the result starts with reference count 1. Always succeeds.
    bool CreateMaterial(const cS3DResMaterial& resource, cS3DMaterial** out);
    bool CreateMaterialVariant(cS3DMaterial*, std::uint32_t, cS3DMaterial**) { return false; }
    bool CreateMaterialInstance(std::uint32_t, void**) { return false; }
    bool CreateMaterialInstance(const char*, void**) { return false; }
    void GetStatsString(cIGZString& out);
};

// FSH texture resource: persisted container + the buffer description of its first frame.
struct S3DTextureBufInfo {
    std::uint64_t fields[5] = {};        // +0x50..+0x70 (the +0x70 slot owns a delete[] buffer)
    bool ready = false;                  // +0x78
};
class cS3DFSHTexture : public cGZPersistResource {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;   // 1 / 0x456b8f1d / 0x1ab50c3f
    S3DTextureBufInfo* GetBufInfo() { return &bufInfo_; }
    FshFile& Shape() { return shape_; }
    // The pixel buffer slot (+0x70) is freed with the texture.
    void SetPixelBuffer(std::uint8_t* pixels) { delete[] reinterpret_cast<std::uint8_t*>(bufInfo_.fields[4]); bufInfo_.fields[4] = reinterpret_cast<std::uint64_t>(pixels); }
protected:
    ~cS3DFSHTexture() override { delete[] reinterpret_cast<std::uint8_t*>(bufInfo_.fields[4]); bufInfo_.fields[4] = 0; }
private:
    FshFile shape_;                      // +0x28 cS3DShapeFile
    S3DTextureBufInfo bufInfo_;          // +0x50
};

}  // namespace s3d
