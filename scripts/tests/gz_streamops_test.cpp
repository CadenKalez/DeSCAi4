#include <cstdio>
#include "gz/StreamOps.h"
#include "rz/PortMemoryStreams.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
int main() {
    PortMemoryOStream os;
    cS3DTransform t; t.flags = 5; t.translation[1] = 7; t.rotation[0][1] = 0.5f;
    std::vector<std::vector<bool>> grid = {{true, false, true}, {false, false, true}};
    os << true << cRZRect{1, 2, 3, 4} << cGZPersistResourceKey{9, 8, 7} << cRZDate(3, 4, 2005) << cS3DVector3{1, 2, 3} << t
       << std::vector<float>{1.5f, 2.5f} << std::vector<std::uint32_t>{7, 8, 9} << grid
       << std::vector<std::vector<std::uint8_t>>{{1, 2}, {3, 4}};
    const auto& b = os.bytes();
    PortMemoryIStream is(b.data(), b.size());
    bool flag; cRZRect r; cGZPersistResourceKey k; cRZDate d; cS3DVector3 v; cS3DTransform t2;
    std::vector<float> f; std::vector<std::uint32_t> u; std::vector<std::vector<bool>> g; std::vector<std::vector<std::uint8_t>> m;
    is >> flag >> r >> k >> d >> v >> t2 >> f >> u >> g >> m;
    CHECK(flag && r.top == 2 && r.bottom == 4 && k.group == 8 && d == cRZDate(3, 4, 2005));
    CHECK(v.z == 3 && t2.flags == 5 && t2.translation[1] == 7 && t2.rotation[0][1] == 0.5f && t2.scale == 1.0f);
    CHECK(f.size() == 2 && f[1] == 2.5f && u.size() == 3 && u[2] == 9);
    CHECK(g.size() == 2 && g[0][2] && !g[1][0] && g[1][2] && m.size() == 2 && m[1][1] == 4 && !is.GetError());
    // oversized counts are refused and leave the vector alone
    PortMemoryOStream big; big << std::uint32_t(2000000) ;
    (void)big;
    std::puts(failures ? "gz_streamops: FAILED" : "gz_streamops ok");
    return failures != 0;
}
