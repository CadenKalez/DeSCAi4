#include "s3d/S3DModel.h"

#include <algorithm>
#include <cstring>

namespace s3d {

std::uint32_t StandardVertexStride(std::int32_t format) {
    switch (format) {
        case 0x20: return 0xc;
        case 0x21: return 0x18;
        case 0x22: return 0x1c;
        case 0x23: return 0x20;
        case 0x24: return 0x28;
        case 0x25: return 0x24;
        case 0x26: return 0x2c;
        case 1: return 0x10;
        case 2: return 0x14;
        case 3: return 0x1c;
        case 10: return 0x18;
        case 11: return 0x20;
        default: break;
    }
    // Only the numbered ids above are used by the game's data: a non-negative unknown id has no
    // packed form (PackStandardVertexFormat returns 0), which gives the "no elements" stride 0.
    if (format >= 0) return 0xc;
    const std::uint32_t f = static_cast<std::uint32_t>(format);
    std::uint32_t stride = ((f >> 0xb) & 0x78) + ((f >> 8) & 0x3c) + ((f >> 6) & 0xc) + (f & 3) * 0xc;
    if ((f & 0xf) != 0)
        stride += ((f >> 0x12) & 0xf0) + ((f >> 5) & 4) + ((f >> 3) & 4) + (f & 0x1c) +
                  (((f >> 6) & 1) ? 0 : 0) /* -(x&1)&0xc */ + (-((f >> 6) & 1) & 0xc) + ((f >> 0x12) & 0xf) * 0xc;
    return stride;
}

namespace {
struct Reader {
    const std::uint8_t* p;
    std::size_t size, pos = 0;
    bool ok = true;
    bool Need(std::size_t n) {
        if (!ok || pos + n > size) { ok = false; return false; }
        return true;
    }
    std::uint8_t U8() { if (!Need(1)) return 0; return p[pos++]; }
    std::uint16_t U16() { if (!Need(2)) return 0; std::uint16_t v; std::memcpy(&v, p + pos, 2); pos += 2; return v; }
    std::int16_t S16() { return static_cast<std::int16_t>(U16()); }
    std::uint32_t U32() { if (!Need(4)) return 0; std::uint32_t v; std::memcpy(&v, p + pos, 4); pos += 4; return v; }
    float F32() { std::uint32_t v = U32(); float f; std::memcpy(&f, &v, 4); return f; }
    bool Tag(const char* tag) {
        if (!Need(4)) return false;
        const bool same = std::memcmp(p + pos, tag, 4) == 0;
        pos += 4;
        return same;
    }
    bool Bytes(void* out, std::size_t n) {
        if (!Need(n)) return false;
        if (n) std::memcpy(out, p + pos, n);
        pos += n;
        return true;
    }
    std::string Str(std::size_t n) {
        std::string s;
        if (!Need(n)) return s;
        s.assign(reinterpret_cast<const char*>(p + pos), n);
        pos += n;
        // the original stores the bytes as a C string: stop at the first NUL
        s.resize(std::strlen(s.c_str()));
        return s;
    }
};

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
}  // namespace

bool Model::Parse(const std::uint8_t* data, std::size_t size) {
    Reader r{data, size};
    if (!r.Tag("3DMD")) return false;
    r.U32();                               // chunk length
    if (!r.Tag("HEAD")) return false;
    r.U32();
    major = r.U16();
    minor = r.U16();
    if (!r.ok) return false;
    if (!(major == 0 || (major == 1 && minor < 6))) return false;
    const bool oldVertexFormat = major == 1 && minor < 4;

    // ---- VERT ----
    if (!r.Tag("VERT")) return false;
    r.U32();
    std::uint32_t count = r.U32();
    if (!r.ok) return false;
    vertexGroups.assign(count, VertexGroup{});
    for (VertexGroup& g : vertexGroups) {
        r.U16();                           // flags (unused by the loader)
        g.count = r.U16();
        if (oldVertexFormat) {
            g.format = r.U16();
            g.stride = r.U16();
        } else {
            g.format = r.U32();
            g.stride = static_cast<std::uint16_t>(StandardVertexStride(static_cast<std::int32_t>(g.format)));
        }
        g.data.resize(std::size_t(g.stride) * g.count);
        if (!r.Bytes(g.data.data(), g.data.size())) return false;
        if (g.count && g.stride >= 12) {
            std::memcpy(g.min, g.data.data(), 12);
            std::memcpy(g.max, g.data.data(), 12);
            for (std::uint32_t i = 1; i < g.count; ++i) {
                float v[3];
                std::memcpy(v, g.data.data() + std::size_t(i) * g.stride, 12);
                for (int k = 0; k < 3; ++k) {
                    g.min[k] = std::min(g.min[k], v[k]);
                    g.max[k] = std::max(g.max[k], v[k]);
                }
            }
        }
    }

    // ---- INDX ----
    if (!r.Tag("INDX")) return false;
    r.U32();
    count = r.U32();
    if (!r.ok) return false;
    indexGroups.assign(count, {});
    for (auto& g : indexGroups) {
        r.U16();                           // flags
        const std::uint16_t elementSize = r.U16();
        const std::uint16_t n = r.U16();
        if (!r.ok) return false;
        if (elementSize == 2 && n) {
            g.resize(n);
            if (!r.Bytes(g.data(), std::size_t(n) * 2)) return false;
        }
    }

    // ---- PRIM ----
    if (!r.Tag("PRIM")) return false;
    r.U32();
    count = r.U32();
    if (!r.ok) return false;
    primitiveGroups.assign(count, {});
    for (auto& g : primitiveGroups) {
        const std::uint16_t n = r.U16();
        if (!r.ok) return false;
        g.primitives.resize(n);
        for (Primitive& p : g.primitives) {
            p.type = r.U32();
            p.first = r.U32();
            p.count = r.U32();
        }
        if (!r.ok) return false;
    }

    // ---- MATS ----
    if (!r.Tag("MATS")) return false;
    r.U32();
    count = r.U32();
    if (!r.ok) return false;
    materials.assign(count, {});
    const bool newTextureFields = !(major < 2 && (major != 1 || minor < 5));
    for (Material& m : materials) {
        m.flags = r.U32();
        m.alphaFunc = r.U8();
        m.depthFunc = r.U8();
        m.srcBlend = r.U8();
        m.dstBlend = r.U8();
        m.alphaThreshold = r.U16();
        m.materialClass = r.U32();
        m.reserved = r.U8();
        const std::uint8_t textureCount = r.U8();
        if (!r.ok) return false;
        m.textures.resize(textureCount);
        for (TextureRef& t : m.textures) {
            t.instance = r.U32();
            t.wrapU = r.U8();
            t.wrapV = r.U8();
            if (newTextureFields) {
                t.magFilter = r.U8();
                t.minFilter = r.U8();
            }
            t.animRate = r.U16();
            t.animMode = r.U16();
            const std::uint8_t len = r.U8();
            t.name = r.Str(len);
            if (!r.ok) return false;
        }
    }

    // ---- ANIM ----
    if (!r.Tag("ANIM")) return false;
    r.U32();
    frameCount = r.U16();
    frameRate = r.U16();
    playMode = static_cast<std::uint8_t>(r.U16());
    animFlags = static_cast<std::uint8_t>(r.U32());
    displacement = r.F32();
    const std::uint16_t meshCount = r.U16();
    if (!r.ok) return false;
    meshes.clear();
    meshes.reserve(meshCount);
    for (std::uint16_t i = 0; i < meshCount; ++i) {
        AnimMesh mesh;
        const std::uint8_t nameLen = r.U8();
        mesh.flags = r.U8();
        mesh.name = r.Str(nameLen);
        mesh.frames.resize(frameCount);
        if (!r.Bytes(mesh.frames.data(), std::size_t(frameCount) * sizeof(FrameRef))) return false;
        for (const FrameRef& f : mesh.frames) {
            if (f.vertexGroup >= vertexGroups.size() || f.indexGroup >= indexGroups.size() ||
                f.primitiveGroup >= primitiveGroups.size() || f.materialGroup >= materials.size())
                return true;      // the original stops adding meshes at the first bad reference (keeps the rest unread)
        }
        meshes.push_back(std::move(mesh));
    }

    // ---- PROP (>= 1.1) ----
    if (major > 1 || (major == 1 && minor != 0)) {
        if (!r.Tag("PROP")) return false;
        r.U32();
        count = r.U32();
        if (!r.ok) return false;
        properties.assign(count, {});
        for (Property& p : properties) {
            p.a = r.S16();
            p.b = r.S16();
            p.name = r.Str(r.U8());
            p.value = r.Str(r.U8());
            if (!r.ok) return false;
        }
        std::sort(properties.begin(), properties.end(), [](const Property& x, const Property& y) {
            if (x.a != y.a) return x.a < y.a;
            return x.b < y.b;
        });
    }

    // ---- REGP (>= 1.2) ----
    if (major > 1 || (major == 1 && minor > 1)) {
        if (!r.Tag("REGP")) return false;
        r.U32();
        count = r.U32();
        if (!r.ok) return false;
        for (std::uint32_t i = 0; i < count; ++i) {
            std::string name = Lower(r.Str(r.U8()));
            const std::uint16_t n = r.U16();
            std::vector<RegPoint> points(n);
            for (RegPoint& p : points)
                for (float& v : p.v) v = r.F32();
            if (!r.ok) return false;
            regPoints.emplace(std::move(name), std::move(points));
        }
    }
    return r.ok;
}

bool Model::GetBounds(float lo[3], float hi[3]) const {
    bool any = false;
    for (const AnimMesh& m : meshes) {
        if (m.frames.empty()) continue;
        const VertexGroup& g = vertexGroups[m.frames[0].vertexGroup];
        if (!g.count) continue;
        for (int k = 0; k < 3; ++k) {
            lo[k] = any ? std::min(lo[k], g.min[k]) : g.min[k];
            hi[k] = any ? std::max(hi[k], g.max[k]) : g.max[k];
        }
        any = true;
    }
    return any;
}

}  // namespace s3d
