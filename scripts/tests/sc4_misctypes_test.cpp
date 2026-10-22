#include <cmath>
#include <cstdio>
#include <cstring>

#include "gz/SparseMatrix.h"
#include "sc4/MiscTypes.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)
using namespace sc4;

struct FakeTable : ISpriteAnimTable {
    int refs = 1; SpriteAnimTableHeader header{};
    alignas(8) unsigned char block[0x20 + 16] = {};
    FakeTable() { header = {8, 16, 32, 1.0f, 1.0f + 6.2831855f / 16 * 7, 0.5f, 0.5f + 6.2831855f / 32 * 3}; std::memcpy(block, &header, sizeof header); }
    void AddRef() override { ++refs; }
    void Release() override { --refs; }
    const SpriteAnimTableHeader* GetData() override { return reinterpret_cast<const SpriteAnimTableHeader*>(block); }
};

int main() {
    // ---- rectangles
    SC4RectInt32 a{0, 0, 5, 5}, b{3, 4, 9, 7}, r;
    r.UnionRect(a, b);
    CHECK(r.x0 == 0 && r.z0 == 0 && r.x1 == 9 && r.z1 == 7);
    CHECK(r.IntersectRect(a, b) && r.x0 == 3 && r.z0 == 4 && r.x1 == 5 && r.z1 == 5);
    CHECK(r.IntersectRect(b, a) && r.x0 == 3 && r.x1 == 5);
    SC4RectInt32 far{6, 0, 9, 3};
    CHECK(!r.IntersectRect(a, far) && r.x0 == 0 && r.z0 == 0 && r.x1 == 0 && r.z1 == 0);
    SC4RectInt32 touch{5, 5, 8, 8};
    CHECK(r.IntersectRect(a, touch) && r.x0 == 5 && r.z0 == 5 && r.x1 == 5 && r.z1 == 5);   // touching counts
    SC4RectFloat32 fa{0, 0, 5.5f, 5}, fb{5.5f, 1, 8, 2}, fr;
    CHECK(fr.IntersectRect(fa, fb) && fr.x0 == 5.5f && fr.x1 == 5.5f && fr.z0 == 1 && fr.z1 == 2);
    SC4RectFloat32 ff{5.6f, 0, 9, 1};
    CHECK(!fr.IntersectRect(fa, ff) && fr.x1 == 0);

    // ---- HTTP headers
    HTTPMessage msg;
    msg.headers = {{"Content-Type", "text/html"}, {"set-cookie", "a=1"}, {"Set-Cookie", "b=2"}};
    std::string value = "stale";
    CHECK(msg.GetMessageHeaderEntryCopy("CONTENT-TYPE", &value, 0) && value == "text/html");
    CHECK(msg.GetMessageHeaderEntryCopy("Set-Cookie", &value, 0) && value == "a=1");
    CHECK(msg.GetMessageHeaderEntryCopy("Set-Cookie", &value, 1) && value == "b=2");
    CHECK(!msg.GetMessageHeaderEntryCopy("Set-Cookie", &value, 2) && value.empty());
    CHECK(msg.GetMessageHeaderEntryCopy("content-type", nullptr, 0));
    CHECK(!msg.GetMessageHeaderEntryCopy("Missing", nullptr, 0));

    CHECK(msg.GetMessageHeaderEntryCopy(42, &value, 0) && value == "text/html");   // 42 = Content-Type
    CHECK(msg.GetMessageHeaderEntryCopy(52, &value, 1) && value == "b=2");        // 52 = Set-Cookie
    CHECK(!msg.GetMessageHeaderEntryCopy(0, &value, 0) && !msg.GetMessageHeaderEntryCopy(55, &value, 0));
    CHECK(std::string(HTTPMessage::MessageHeaderFieldToFieldString(1)) == "Cache-Control");
    CHECK(std::string(HTTPMessage::MessageHeaderFieldToFieldString(54)) == "Cookie");
    CHECK(HTTPMessage::MessageHeaderFieldToFieldString(0) == nullptr && HTTPMessage::MessageHeaderFieldToFieldString(55) == nullptr);
    {
        std::string parts[4];
        CHECK(HTTPMessage::GetSeparatedStringList("a, b ,c", parts, 4, false, ',', true) == 3 && parts[0] == "a" && parts[1] == "b" && parts[2] == "c");
        CHECK(HTTPMessage::GetSeparatedStringList("a, b ,c", parts, 4, false, ',', false) == 3 && parts[1] == " b ");
        CHECK(HTTPMessage::GetSeparatedStringList("x=\"p,q\", y", parts, 4, true, ',', true) == 2 && parts[0] == "x=p,q" && parts[1] == "y");
        CHECK(HTTPMessage::GetSeparatedStringList("1,2,3,4,5", parts, 2, false, ',', false) == 2 && parts[0] == "1" && parts[1] == "2");   // stops when full
        CHECK(HTTPMessage::GetSeparatedStringList("a,", parts, 4, false, ',', false) == 1 && parts[0] == "a");
        CHECK(HTTPMessage::GetSeparatedStringList(",", parts, 4, false, ',', false) == 1 && parts[0].empty());
        CHECK(HTTPMessage::GetSeparatedStringList("   ", parts, 4, false, ',', true) == 1 && parts[0].empty());   // counted before trimming
        CHECK(HTTPMessage::GetSeparatedStringList("\"\"", parts, 4, true, ',', true) == 0 && parts[0] == "\"\"");   // quirk: no part -> whole text in out[0], 0 returned
        CHECK(HTTPMessage::GetSeparatedStringList("  q  ", parts, 4, false, ',', true) == 1 && parts[0] == "q");
        CHECK(HTTPMessage::GetSeparatedStringList("", parts, 4, false, ',', true) == 0);
        CHECK(HTTPMessage::GetSeparatedStringList("a", parts, 0, false, ',', true) == 0);
    }

    // ---- refcounted buffers
    auto* shared = new SharedMemoryPointer; shared->data = new unsigned char[4]; shared->refCount = 3;
    CHECK(shared->Release() == 2 && shared->Release() == 1 && shared->Release() == 0);   // the last release frees
    auto* cache = new CacheData; cache->data = new unsigned char[4]; cache->refCount = 2;
    CHECK(cache->Release() == 1 && cache->Release() == 0);
    auto* fresh = new CacheData;   // never counted: released at once
    CHECK(fresh->Release() == 0);

    // ---- records
    CookieData cookie;
    cookie.field0 = "name"; cookie.attributes = {{"k", "v"}}; cookie.field60 = "x"; cookie.numbers[8] = 99; cookie.flags = 7;
    CookieData copy(cookie);
    CHECK(copy.field0 == "name" && copy.attributes.size() == 1 && copy.field60 == "x" && copy.numbers[8] == 99 && copy.flags == 7);
    TVNode node;
    node.label = cRZString("l"); node.tag = cRZString("t"); node.text = cRZString("tx"); node.numbers[0x29] = 5;
    TVNode node2(node);
    CHECK(std::string(node2.label.ToChar()) == "l" && std::string(node2.text.ToChar()) == "tx" && node2.numbers[0x29] == 5);
    WinGridCellData cell;
    CHECK(cell.first == 0 && cell.text.Strlen() == 0 && cell.tail[2] == 0);
    HELI_TYPE_DATA heli;
    for (int i = 0; i < 32; ++i) heli.words[i] = std::uint32_t(100 + i);
    HELI_TYPE_DATA heli2(heli);
    CHECK(heli2.words[0] == 0 && heli2.words[1] == 101 && heli2.words[31] == 131);   // quirk: the first word is not copied
    CompareCitiesForExport cities;
    int c1 = 0, c2 = 0;
    cities.scores = {{&c1, 1.5f}, {&c2, 2.5f}};
    CompareCitiesForExport cities2(cities);
    CHECK(cities2.scores.size() == 2 && cities2.scores[&c2] == 2.5f);
    cRZSparseMatrix<int>::Row row{{1, 10}, {5, 50}};
    auto rowCopy = row;
    CHECK(rowCopy.size() == 2 && rowCopy[5] == 50);

    // ---- sprite table
    FakeTable table;
    cAnimatedSpriteTableData data;
    data.Init(&table);
    CHECK(table.refs == 2 && data.table == &table && data.frames == table.block + 0x20);
    CHECK(data.countA == 8 && std::fabs(data.stepA - 6.2831855f / 8) < 1e-6f && std::fabs(data.stepB - 6.2831855f / 16) < 1e-6f);
    CHECK(data.countB == 8 && data.countC == 4 && data.frameCount == 32);
    data.Init(nullptr);   // clears
    CHECK(table.refs == 1 && data.table == nullptr);
    data.Init(&table);
    data.Init(&table);    // re-init: drops the reference it held and takes one again
    CHECK(table.refs == 2);
    std::printf(g_fail ? "FAILED %d\n" : "sc4_misctypes OK\n", g_fail);
    return g_fail ? 1 : 0;
}
