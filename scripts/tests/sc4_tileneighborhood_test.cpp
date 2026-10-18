#include <cstdio>
#include <vector>

#include "sc4/TileNeighborhood.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

struct Tokens final : ITileTokenSource {
    std::vector<const char*> list;
    size_t i = 0;
    const char* GetToken() override { return i < list.size() ? list[i++] : nullptr; }
};

int main() {
    cSC4TileNeighborhoodCompact n;
    n.SetEdgeCondition(0, 0x02);
    n.SetEdgeCondition(1, 0x0b);
    n.SetEdgeCondition(3, 0x01);
    CHECK(n.Packed() == 0x01000b02 && n.GetEdgeCondition(1) == 0x0b && n.GetEdgeCondition(5) == 0x0b);   // edge index wraps mod 4
    CHECK(n.HasConnectionOnEdge(0) && !n.HasConnectionOnEdge(2) && cSC4TileNeighborhoodCompact::HasConnectionOnEdge(0x300, 1));

    int straight[4], diagonal[4], sc = 9, dc = 9;
    n.GetStraightAndDiagonalEdges(straight, diagonal, sc, dc);
    CHECK(sc == 1 && straight[0] == 0 && dc == 2 && diagonal[0] == 1 && diagonal[1] == 3);

    // mirror: edges 0 and 2 trade places, kinds 1 and 3 swap, straight edges swap their two flag bits
    cSC4TileNeighborhoodCompact mirror;
    n.GetMirrorImageEquivalent(mirror);
    CHECK(mirror.GetEdgeCondition(2) == 0x02);          // straight edge moved to 2 (bits 4/5 were clear)
    CHECK(mirror.GetEdgeCondition(1) == 0x09);              // 0x0b: kind 3 -> 1, bit 3 kept
    CHECK(mirror.GetEdgeCondition(3) == 0x03);          // kind 1 -> 3

    // merge: none takes the incoming kind, diagonal 3 / 1 upgrade to straight on a straight edge
    cSC4TileNeighborhoodCompact a(0x00030100), b(0x02020202);
    a.Merge(b);
    CHECK(a.GetEdgeCondition(0) == 2 && a.GetEdgeCondition(1) == 2 && a.GetEdgeCondition(2) == 2 && a.GetEdgeCondition(3) == 2);
    cSC4TileNeighborhoodCompact c(0x05), zero;
    c.Merge(zero);
    CHECK(c.Packed() == 0x05);

    Tokens t;
    t.list = {"12", "3", "0", "25"};
    cSC4TileNeighborhoodCompact r;
    CHECK(r.Read(t));
    CHECK(r.GetEdgeCondition(0) == ((1 << 3) | 2) && r.GetEdgeCondition(1) == 3 && r.GetEdgeCondition(2) == 0 &&
          r.GetEdgeCondition(3) == ((2 << 3) | 5));
    r.ResetEdgeMinorTypes();
    CHECK(r.GetEdgeCondition(0) == 2 && r.GetEdgeCondition(3) == 5);
    r.SetEdgeTypeMinor(0, 3);
    CHECK(r.GetEdgeCondition(0) == ((3 << 3) | 2));
    Tokens shortTokens;
    shortTokens.list = {"1", "2"};
    CHECK(!r.Read(shortTokens));
    cSC4TileNeighborhoodCompact d(0x00000004);
    d.SetEdgeConnectionDirection(0, 1);
    CHECK(d.GetEdgeCondition(0) == (4 | 3));              // keeps flag bit 2, table[1] = 3
    d.SetEdgeConnectionDirection(1, 1);                   // edge 1: (1 + 2) & 7 = 3 -> table[3] = 1
    CHECK(d.GetEdgeCondition(1) == 1);

    // lot and configuration
    auto* config = new cSC4NetworkLotConfiguration();
    config->AddRef();
    config->GetObjectArray().resize(3);
    config->SetID(77);
    config->SetNetworkPieceId(5);
    cRZString name("road"), out;
    config->SetName(name);
    config->GetName(out);
    CHECK(config->GetID() == 77 && config->GetNetworkPieceId() == 5 && std::string(out.Data()) == "road");
    CHECK(config->TileNeighborhood() == 0x0f0f0f0f);
    auto* lot = new cSC4NetworkLot();
    lot->AddRef();
    CHECK(lot->GetLotConfigurationObjectCount() == 0 && lot->GetLotConfigurationObjectArray().empty());
    lot->SetConfiguration(config);
    CHECK(lot->GetLotConfigurationObjectCount() == 3);
    void* raw = nullptr;
    CHECK(lot->QueryInterface(0x89a5ba66, &raw) && !lot->QueryInterface(2, &raw));
    lot->Release();
    lot->Release();
    config->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
