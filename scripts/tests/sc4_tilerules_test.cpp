// The network tile-rule engine (cSC4FileTokenizer, cSC4TileInfo, tCompactNeighborInfo, cSC4TileRule, cSC4TileNeighborhood,
// cSC4TileEvaluator) — unit test.
#include "sc4/TileRules.h"

#include <cstdio>
#include <map>

namespace {
int g_failures = 0;
#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++g_failures;                                                             \
        }                                                                             \
    } while (false)
using namespace sc4;

std::uint32_t Pack(int e0, int e1, int e2, int e3) {
    const auto entry = [](int n) { return static_cast<std::uint32_t>(((n / 10) * 8 & 0xf8) | (n % 10)); };
    return entry(e0) | entry(e1) << 8 | entry(e2) << 16 | entry(e3) << 24;
}

class FakeNeighborhood final : public cSC4TileNeighborhood {
public:
    std::map<int, std::uint32_t> cells;   // meta -> compact
    std::map<int, bool> invalid;
    int computed = 0;
    bool IsValidSolutionPos(std::uint8_t meta) override { return !invalid[meta]; }
    void ComputeCompactRepresentation(std::uint8_t meta, cSC4TileNeighborhoodCompact& out) override {
        ++computed;
        out = cSC4TileNeighborhoodCompact(cells.count(meta) ? cells[meta] : 0);
    }
};
}  // namespace

int main() {
    // the tokenizer: comments, blank lines, the trailing comma, pushback
    cSC4FileTokenizer tokens;
    CHECK(tokens.InitFromText("; comment\n\n a,b ,\r\nc\n"));
    CHECK(!tokens.InitFromText("x"));           // only once
    CHECK(std::string(tokens.GetToken()) == " a");
    CHECK(std::string(tokens.GetToken()) == "b ");
    tokens.PushBack();
    CHECK(std::string(tokens.GetToken()) == "b ");
    CHECK(std::string(tokens.GetToken()) == "c");
    CHECK(tokens.GetToken() == nullptr);

    // meta positions and tile infos
    std::int8_t dx, dz;
    cSC4TileInfo::ConvertMetaToRelativePos(5, dx, dz);
    CHECK(dx == 1 && dz == 0);
    CHECK(cSC4TileInfo::ConvertRelativePosToMeta(1, 0) == 5);
    CHECK(RotateMetaPos(1, 1) == 3 && RotateMetaPos(8, 1) == 2 && RotateMetaPos(9, 1) == 13 && RotateMetaPos(0, 3) == 0);
    cSC4TileInfo tile{0x5f00, 1, 0};
    cSC4TileInfo turned;
    tile.GetRotationalEquivalent(1, turned);
    CHECK(turned.id == 0x5f00 && turned.metaPos == 3 && turned.rotation == 1);
    cSC4TileInfo flipped{0x5f00, 0, 0x81};
    flipped.GetRotationalEquivalent(1, turned);
    CHECK(turned.rotation == (0x80 | 0));          // a flipped tile turns the other way
    std::uint32_t x, z;
    tile.GetCell(10, 20, x, z);
    CHECK(x == 9 && z == 20);

    // rules: a straight street (2 on edges 1 and 3), neighbour 5 must be any street on its west edge
    const char* text =
        "#Transmogrify\n"
        "; straight street\n"
        "1,0,2,0,2\n"
        "2,5,?,?,?,2\n"
        "3,0,0x5f500200,0,0\n"
        "notrans\n"
        "1,1,0,0,0\n"
        "3,0,0x5f500300,0,1\n";
    cSC4TileEvaluator evaluator;
    CHECK(evaluator.AddRulesFromText(text));
    // the straight rule: its neighbour condition moves with every turn and mirror (meta 5 / 7 / 1 / 3 with the street on a
    // different edge each time), so all 8 variants differ by condition and are kept; notrans adds 1
    CHECK(evaluator.Rules().size() == 9);

    FakeNeighborhood neighborhood;
    neighborhood.center_ = Pack(0, 2, 0, 2);
    neighborhood.cells[5] = Pack(0, 0, 0, 2);
    const std::vector<cSC4TileInfo>* solutions = nullptr;
    CHECK(evaluator.Lookup(neighborhood, solutions));
    CHECK(solutions && solutions->size() == 1 && (*solutions)[0].id == 0x5f500200);
    CHECK(neighborhood.computed == 1);
    CHECK(evaluator.Lookup(neighborhood, solutions) && neighborhood.computed == 1);   // cached

    FakeNeighborhood wrong;
    wrong.center_ = Pack(0, 2, 0, 2);
    wrong.cells[5] = Pack(0, 0, 0, 0);
    CHECK(!evaluator.Lookup(wrong, solutions) && solutions == nullptr);

    FakeNeighborhood blocked;
    blocked.center_ = Pack(1, 0, 0, 0);
    blocked.invalid[0] = true;
    CHECK(!evaluator.Lookup(blocked, solutions));
    blocked.invalid[0] = false;
    CHECK(evaluator.Lookup(blocked, solutions) && (*solutions)[0].rotation == 0x80);

    // a symmetric rule without conditions repeats itself: the four turns of a cross are one rule
    cSC4TileEvaluator cross;
    CHECK(cross.AddRulesFromText("#Transmogrify\n1,2,2,2,2\n3,0,0x5f500400,0,0\n"));
    CHECK(cross.Rules().size() == 1);

    // a file without a '#' header is refused
    cSC4TileEvaluator refused;
    CHECK(!refused.AddRulesFromText("1,0,2,0,2\n"));

    // the network types' evaluators: only the street files (instances 10 complex, 9 simple) exist here
    SetTileRuleResourceLoader([](const cGZPersistResourceKey& key, std::string& out) {
        if (key.type != kNetworkRulesType || key.group != kNetworkRulesGroup || (key.instance != 9 && key.instance != 10)) return false;
        out = key.instance == 10 ? "#Complex\n1,2,2,2,2\n3,0,0x5f500400,0,0\n" : "#Simple\n1,0,2,0,2\n3,0,0x5f500200,0,0\n";
        return true;
    });
    CHECK(LoadNetworkTileEvaluators() == 2);
    CHECK(NetworkTileEvaluators(3).size() == 2);                       // complex first
    CHECK(NetworkTileEvaluators(3)[0]->Rules().begin()->second.solutions[0].id == 0x5f500400);
    CHECK(NetworkTileEvaluators(0).size() == 2 && NetworkTileEvaluators(0)[0]->Rules().empty());   // pushed although missing
    CHECK(NetworkTileEvaluators(5).empty());                           // the power line has no rules
    CHECK(LoadNetworkTileEvaluators() == 2 && NetworkTileEvaluators(3).size() == 2);   // a reload replaces them

    if (g_failures == 0) std::puts("sc4_tilerules ok");
    return g_failures == 0 ? 0 : 1;
}
