#include <cstdio>

#include "sc4/CatalogItem.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Icon final : cIGZUnknown {
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
struct Manager final : IGlyphTextureManager {
    int refs = 0, unused = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void SignalUnused(cSC4GlyphTextureInfo*) override { ++unused; }
};
struct Binding final : IGlyphTextureBinding {
    int released = 0;
    void Release() override { ++released; }
};
}  // namespace

int main() {
    Icon icon;
    auto* item = new cSC4CatalogItem();
    item->AddRef();
    CHECK(item->GetEnabled() && item->GetCost() == 0 && item->GetIcon() == nullptr);
    item->SetName(cRZString("Road")); item->SetCost(12); item->SetItemType(3); item->SetItemID(99); item->SetIcon(&icon);
    item->SetEnabled(false);
    CHECK(!item->GetEnabled() && item->GetCost() == 12 && item->GetItemType() == 3 && item->GetItemID() == 99);
    CHECK(std::string(item->GetName().ToChar()) == "Road" && icon.refs == 1);
    void* out = nullptr;
    CHECK(item->QueryInterface(kGZIID_cISC4CatalogItem, &out) && out == item && !item->QueryInterface(5, &out));
    CHECK(item->Release() == 1);
    CHECK(item->Release() == 0 && icon.refs == 0);

    Manager mgr; Binding binding;
    auto* info = new cSC4GlyphTextureInfo(cRZString("A"), 256, 128, 16, 20, {0.1f, 0.2f, 0.3f, 0.4f}, &binding);
    info->SetManager(&mgr);
    CHECK(mgr.refs == 1);
    CHECK(info->GetTextureSize().width == 256 && info->GetGlyphSize().height == 20);
    cRZString glyph; info->GetGlyph(glyph);
    CHECK(std::string(glyph.ToChar()) == "A");
    GlyphTexCoords c; info->GetGlyphTextureCoordinates(c);
    CHECK(c.right == 0.3f);
    info->AddRef();                          // 2
    CHECK(info->Release() == 0 && mgr.unused == 1);   // the manager takes over: not destroyed
    info->AddRef();                          // 2 again
    info->AddRef();                          // 3
    CHECK(info->Release() == 2);
    info->SetManager(nullptr);
    CHECK(mgr.refs == 0);
    CHECK(info->Release() == 0 && binding.released == 1);   // no manager: destroyed
    std::printf(failures ? "FAILED\n" : "sc4_catalogitem ok\n");
    return failures != 0;
}
