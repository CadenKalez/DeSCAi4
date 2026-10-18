// cS3DMeshAndMaterial (12 functions, 0x1000d8802..0x1000d8944, iid 0x26a68448): the mesh and material lists of a
// loaded model (+8 meshes of 0x18 bytes, +0x20 materials of 0x50 bytes, +0x38 refcount). The element types are
// kept as raw records here; the element layouts belong to cS3DMesh / cS3DMaterial.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

#include "gz/cIGZUnknown.h"

namespace s3d {

constexpr std::uint32_t kGZIID_cIS3DMeshAndMaterial = 0x26a68448;
using MeshRecord = std::array<std::uint8_t, 0x18>;
using MaterialRecord = std::array<std::uint8_t, 0x50>;

class cS3DMeshAndMaterial : public cIGZUnknown {
public:
    virtual ~cS3DMeshAndMaterial() = default;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != 1 && iid != kGZIID_cIS3DMeshAndMaterial) return false;
        *out = this;
        AddRef();
        return true;
    }
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override {
        if (refCount_ > 1) return --refCount_;
        delete this;
        return 0;
    }
    int MeshCount() const { return static_cast<int>(meshes_.size()); }
    MeshRecord& Mesh(unsigned index) { return meshes_[index]; }
    const MeshRecord& Mesh(unsigned index) const { return meshes_[index]; }
    MaterialRecord& Material(unsigned index) { return materials_[index]; }
    const MaterialRecord& Material(unsigned index) const { return materials_[index]; }
    std::vector<MeshRecord>& Meshes() { return meshes_; }
    std::vector<MaterialRecord>& Materials() { return materials_; }

protected:
    std::vector<MeshRecord> meshes_;           // +8
    std::vector<MaterialRecord> materials_;    // +0x20
    std::uint32_t refCount_ = 0;               // +0x38
};

}  // namespace s3d
