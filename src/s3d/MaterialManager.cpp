#include "s3d/MaterialManager.h"

#include <algorithm>
#include <cstring>

namespace s3d {

bool cS3DMaterialManager::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIS3DMaterialManager) {
        *out = this;
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

bool cS3DMaterialManager::CreateMaterial(const cS3DResMaterial& resource, cS3DMaterial** out) {
    auto* material = new cS3DMaterial;
    material->flags = resource.flags | 0x180;
    material->b7 = resource.field4;
    material->b8 = resource.field9;
    material->b6 = resource.field5;
    material->b9 = resource.field6;
    material->b10 = resource.field7;
    material->layerCount = static_cast<std::uint8_t>(resource.layerCount);
    const std::uint32_t copied = std::min<std::uint32_t>(resource.layerCount, 2);
    for (std::uint32_t i = 0; i < copied; ++i) {
        S3DMaterialLayer& layer = material->layers[i];
        std::memcpy(layer.sampler, resource.layers[i].samplerBytes, 4);
        layer.pad15 = 0;
        layer.textureId = resource.layers[i].textureId;
    }
    for (std::uint32_t i = copied; i < 2; ++i) material->layers[i].used = 0xff;   // unused layers
    material->state = 1;
    *out = material;
    ++material->refCount;
    return true;
}

void cS3DMaterialManager::GetStatsString(cIGZString& out) { out.Sprintf("Nothing to report"); }

bool cS3DFSHTexture::QueryInterface(std::uint32_t iid, void** out) {
    *out = nullptr;
    if (iid == kGZIID_cIGZPersistResource) *out = static_cast<cIGZPersistResource*>(this);
    else if (iid == kGZIID_cIS3DFSHTexture) *out = this;
    else return false;
    AddRef();
    return true;
}

}  // namespace s3d
