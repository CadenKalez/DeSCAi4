#include "gz/StreamOps.h"

cIGZOStream& operator<<(cIGZOStream& s, bool v) { s.SetUint8(v ? 1 : 0); return s; }
cIGZIStream& operator>>(cIGZIStream& s, bool& v) {
    std::uint8_t b = 0;
    s.GetUint8(b);
    v = b != 0;
    return s;
}

cIGZOStream& operator<<(cIGZOStream& s, const cRZRect& v) {
    s.SetSint32(v.left); s.SetSint32(v.top); s.SetSint32(v.right); s.SetSint32(v.bottom);
    return s;
}
cIGZIStream& operator>>(cIGZIStream& s, cRZRect& v) {
    s.GetSint32(v.left); s.GetSint32(v.top); s.GetSint32(v.right); s.GetSint32(v.bottom);
    return s;
}

cIGZOStream& operator<<(cIGZOStream& s, const cGZPersistResourceKey& v) {
    s.SetUint32(v.type); s.SetUint32(v.group); s.SetUint32(v.instance);
    return s;
}
cIGZIStream& operator>>(cIGZIStream& s, cGZPersistResourceKey& v) {
    s.GetUint32(v.type); s.GetUint32(v.group); s.GetUint32(v.instance);
    return s;
}

cIGZOStream& operator<<(cIGZOStream& s, const cRZDate& v) { s.SetUint32(v.JulianDay()); return s; }
cIGZIStream& operator>>(cIGZIStream& s, cRZDate& v) {
    std::uint32_t day = 0;
    s.GetUint32(day);
    v.SetJulianDay(day);
    return s;
}

cIGZOStream& operator<<(cIGZOStream& s, const cRZTimer& v) {
    s.SetUint8(static_cast<std::uint8_t>(v.GetUnits()));
    std::uint64_t elapsed = 0;
    v.GetElapsedTimeU64(elapsed);
    s.SetUint64(elapsed);
    s.SetUint8(v.IsTimerRunning() ? 1 : 0);
    return s;
}
cIGZIStream& operator>>(cIGZIStream& s, cRZTimer& v) {
    v.Reset();
    std::uint8_t units = 0;
    s.GetUint8(units);
    v.SetUnits(static_cast<cRZTimer::Units>(units));
    std::uint64_t elapsed = 0;
    s.GetUint64(elapsed);
    v.SetElapsedTimeU64(elapsed);
    std::uint8_t running = 0;
    s.GetUint8(running);
    if (running) v.Start();
    else v.Stop();
    return s;
}

cIGZOStream& operator<<(cIGZOStream& s, const cS3DVector2& v) { s.SetFloat32(v.x); s.SetFloat32(v.y); return s; }
cIGZIStream& operator>>(cIGZIStream& s, cS3DVector2& v) { s.GetFloat32(v.x); s.GetFloat32(v.y); return s; }
cIGZOStream& operator<<(cIGZOStream& s, const cS3DVector3& v) { s.SetFloat32(v.x); s.SetFloat32(v.y); s.SetFloat32(v.z); return s; }
cIGZIStream& operator>>(cIGZIStream& s, cS3DVector3& v) { s.GetFloat32(v.x); s.GetFloat32(v.y); s.GetFloat32(v.z); return s; }

cIGZOStream& operator<<(cIGZOStream& s, const cS3DBoundingBox& v) {
    for (float f : v.min) s.SetFloat32(f);
    for (float f : v.max) s.SetFloat32(f);
    return s;
}
cIGZIStream& operator>>(cIGZIStream& s, cS3DBoundingBox& v) {
    for (float& f : v.min) s.GetFloat32(f);
    for (float& f : v.max) s.GetFloat32(f);
    return s;
}

cIGZOStream& operator<<(cIGZOStream& s, const cS3DMatrix4& v) {
    for (auto& row : v.m)
        for (float f : row) s.SetFloat32(f);
    return s;
}
cIGZIStream& operator>>(cIGZIStream& s, cS3DMatrix4& v) {
    for (auto& row : v.m)
        for (float& f : row) s.GetFloat32(f);
    return s;
}

cIGZOStream& operator<<(cIGZOStream& s, const cS3DTransform& v) {
    s.SetUint8(v.flags);
    if (v.flags & 4)
        for (auto& row : v.rotation)
            for (float f : row) s.SetFloat32(f);
    if (v.flags & 1)
        for (float f : v.translation) s.SetFloat32(f);
    if (v.flags & 2) s.SetFloat32(v.scale);
    return s;
}
cIGZIStream& operator>>(cIGZIStream& s, cS3DTransform& v) {
    std::uint8_t flags = 0;
    s.GetUint8(flags);
    if (v.flags != 0) {     // reset to identity, bumping the change counter
        v = cS3DTransform{v.changeCounter, 0, {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {0, 0, 0}, 1.0f};
        ++v.changeCounter;
    }
    if (flags & 4) {
        for (auto& row : v.rotation)
            for (float& f : row) s.GetFloat32(f);
        v.flags |= 4;
        ++v.changeCounter;
    }
    if (flags & 1) {
        for (float& f : v.translation) s.GetFloat32(f);
        v.flags |= 1;
        ++v.changeCounter;
    }
    if (flags & 2) {
        s.GetFloat32(v.scale);
        v.flags |= 2;
        if (v.scale == 1.0f) v.flags &= ~2;
        ++v.changeCounter;
    }
    return s;
}

cIGZOStream& operator<<(cIGZOStream& s, const std::vector<float>& v) {
    s.SetUint32(static_cast<std::uint32_t>(v.size()));
    for (float f : v) s.SetFloat32(f);
    return s;
}
cIGZIStream& operator>>(cIGZIStream& s, std::vector<float>& v) {
    std::uint32_t count = 0;
    s.GetUint32(count);
    if (count < kStreamVectorLimit) {
        v.resize(count);
        for (float& f : v) s.GetFloat32(f);
    }
    return s;
}

cIGZOStream& operator<<(cIGZOStream& s, const std::vector<std::uint8_t>& v) {
    s.SetUint32(static_cast<std::uint32_t>(v.size()));
    for (std::uint8_t b : v) s.SetUint8(b);
    return s;
}
cIGZIStream& operator>>(cIGZIStream& s, std::vector<std::uint8_t>& v) {
    std::uint32_t count = 0;
    s.GetUint32(count);
    if (count < kStreamVectorLimit) {
        v.resize(count);
        for (std::uint8_t& b : v) s.GetUint8(b);
    }
    return s;
}

cIGZOStream& operator<<(cIGZOStream& s, const std::vector<std::uint32_t>& v) {
    s.SetUint32(static_cast<std::uint32_t>(v.size()));
    for (std::uint32_t x : v) s.SetUint32(x);
    return s;
}
cIGZIStream& operator>>(cIGZIStream& s, std::vector<std::uint32_t>& v) {
    std::uint32_t count = 0;
    s.GetUint32(count);
    if (count < kStreamVectorLimit) {
        v.resize(count);
        for (std::uint32_t& x : v) s.GetUint32(x);
    }
    return s;
}

cIGZOStream& operator<<(cIGZOStream& s, const std::vector<std::vector<std::uint8_t>>& v) {
    s.SetUint32(static_cast<std::uint32_t>(v.size()));
    const std::uint32_t columns = v.empty() ? 0 : static_cast<std::uint32_t>(v[0].size());
    s.SetUint32(columns);
    for (const auto& row : v)
        for (std::uint32_t c = 0; c < columns; ++c) s.SetUint8(row[c]);
    return s;
}
cIGZIStream& operator>>(cIGZIStream& s, std::vector<std::vector<std::uint8_t>>& v) {
    std::uint32_t rows = 0, columns = 0;
    s.GetUint32(rows);
    s.GetUint32(columns);
    if (rows < kStreamVectorLimit && columns < kStreamVectorLimit) {
        v.resize(rows);
        for (auto& row : v) {
            row.resize(columns);
            for (std::uint8_t& b : row) s.GetUint8(b);
        }
    }
    return s;
}

cIGZOStream& operator<<(cIGZOStream& s, const std::vector<std::vector<bool>>& v) {
    s.SetUint32(static_cast<std::uint32_t>(v.size()));
    const std::uint32_t columns = v.empty() ? 0 : static_cast<std::uint32_t>(v[0].size());
    s.SetUint32(columns);
    for (const auto& row : v)
        for (std::uint32_t c = 0; c < columns; ++c) s.SetUint8(row[c] ? 1 : 0);
    return s;
}
cIGZIStream& operator>>(cIGZIStream& s, std::vector<std::vector<bool>>& v) {
    std::uint32_t rows = 0, columns = 0;
    s.GetUint32(rows);
    s.GetUint32(columns);
    if (rows < kStreamVectorLimit && columns < kStreamVectorLimit) {
        v.resize(rows);
        for (auto& row : v) {
            row.assign(columns, false);
            for (std::uint32_t c = 0; c < columns; ++c) {
                std::uint8_t b = 0;
                s.GetUint8(b);
                row[c] = b != 0;
            }
        }
    }
    return s;
}

void LoadLegacyS3DTransform(cIGZIStream& s, cS3DTransform& v) {
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col) s.GetFloat32(v.rotation[row][col]);
    v.flags |= 4;
    ++v.changeCounter;
    for (int i = 0; i < 3; ++i) s.GetFloat32(v.translation[i]);
    v.flags |= 1;
    ++v.changeCounter;
    s.GetFloat32(v.scale);
    if (v.scale == 1.0f) v.flags &= ~2; else v.flags |= 2;
    ++v.changeCounter;
    std::uint32_t counter = 0;
    s.GetUint32(counter);
    v.changeCounter = static_cast<std::uint8_t>(counter);
}
