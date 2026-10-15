// S3D model resource (type 0x5ad0e817), the geometry format of every SC4 building, prop and flora.
// Parsed exactly like cS3DResModelFactory::SerialRead (0x10046695c): chunks "3DMD", "HEAD", "VERT",
// "INDX", "PRIM", "MATS", "ANIM", then "PROP" (version >= 1.1) and "REGP" (>= 1.2).
// All integers are little endian.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace s3d {

constexpr std::uint32_t kResourceTypeModel = 0x5ad0e817;
constexpr std::uint32_t kResourceTypeTexture = 0x7ab50e44;   // FSH

// nRZSimGL::StandardVFStride(): bytes per vertex of a standard vertex format id (packed formats have the
// top bit set and describe element counts in their bit fields).
std::uint32_t StandardVertexStride(std::int32_t format);

struct VertexGroup {
    std::uint32_t format = 2;           // standard format id (2 = position + uv, 20 bytes)
    std::uint16_t stride = 0x14;
    std::uint16_t count = 0;
    std::vector<std::uint8_t> data;     // count * stride bytes
    float min[3] = {0, 0, 0}, max[3] = {0, 0, 0};   // position bounds
};

struct Primitive { std::uint32_t type = 0, first = 0, count = 0; };   // type 0: triangle list
struct PrimitiveGroup { std::vector<Primitive> primitives; };

struct TextureRef {
    std::uint32_t instance = 0;           // FSH instance id (group comes from the model's own key / default)
    std::uint8_t wrapU = 0, wrapV = 0;    // 0 repeat, 1 mirror, 2 clamp
    std::uint8_t magFilter = 0, minFilter = 0;   // version >= 1.5
    std::uint16_t animRate = 0, animMode = 0;
    std::string name;
};

struct Material {
    std::uint32_t flags = 0;
    std::uint8_t alphaFunc = 0, depthFunc = 0, srcBlend = 0, dstBlend = 0;
    std::uint16_t alphaThreshold = 0;
    std::uint32_t materialClass = 0;
    std::uint8_t reserved = 0;
    std::vector<TextureRef> textures;     // up to 4
};

struct FrameRef { std::uint16_t vertexGroup, indexGroup, primitiveGroup, materialGroup; };
struct AnimMesh {
    std::uint8_t flags = 0;
    std::string name;
    std::vector<FrameRef> frames;         // one per animation frame
};

struct Property { std::int16_t a = 0, b = 0; std::string name, value; };
struct RegPoint { float v[7]; };          // translation + rotation, 7 floats as stored

struct Model {
    std::uint16_t major = 0, minor = 0;
    std::vector<VertexGroup> vertexGroups;
    std::vector<std::vector<std::uint16_t>> indexGroups;
    std::vector<PrimitiveGroup> primitiveGroups;
    std::vector<Material> materials;
    // ANIM
    std::uint16_t frameCount = 0, frameRate = 0;
    std::uint8_t playMode = 0;
    std::uint8_t animFlags = 0;
    float displacement = 0;
    std::vector<AnimMesh> meshes;
    std::vector<Property> properties;                       // sorted by (a, b, name)
    std::map<std::string, std::vector<RegPoint>> regPoints;  // lower-cased name

    // Parse a whole resource; returns false when a chunk is malformed (the original fails the read).
    bool Parse(const std::uint8_t* data, std::size_t size);
    // union of the vertex group bounds actually referenced by frame 0 of every mesh
    bool GetBounds(float min[3], float max[3]) const;
};

}  // namespace s3d
