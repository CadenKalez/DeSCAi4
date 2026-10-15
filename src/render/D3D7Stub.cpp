#include "render/D3D7Stub.h"

#include <cstring>

namespace sgl {

namespace {
// The two IIDs as stored by the game (the full 24-byte _GUID is compared; IID_IUnknown is the literal "????" followed by zeros).
const std::uint8_t kIidDirect3DDevice7[24] = {0x79, 0x9e, 0x04, 0xf5, 0x00, 0x00, 0x00, 0x00, 0x61, 0x48, 0xd2, 0x11,
                                              0xa4, 0x07, 0x00, 0xa0, 0xc9, 0x06, 0x29, 0xa8, 0x00, 0x00, 0x00, 0x00};
const std::uint8_t kIidUnknown[24] = {0x3f, 0x3f, 0x3f, 0x3f};
}  // namespace

std::uint32_t cSGLDX7Direct3DDevice7Stub::QueryInterface(const _GUID& iid, void** out) {
    if (std::memcmp(&iid, kIidDirect3DDevice7, sizeof(_GUID)) != 0 && std::memcmp(&iid, kIidUnknown, sizeof(_GUID)) != 0) {
        return kDDErrNoInterface;
    }
    *out = this;
    AddRef();
    return kDDOk;
}

std::int32_t cSGLDX7Direct3DDevice7Stub::Release() {
    const std::int32_t count = --refCount_;
    if (count == 0) delete this;
    return count;
}

}  // namespace sgl
