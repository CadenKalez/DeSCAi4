// cGZScriptVocab test: case-insensitive keyword table, counted values, no replacement on a second Add, the interface
// views (including the LegacyControl.h IVocabulary view, whose slot order must match the binary's vtable), Release.
#include <cstdint>
#include <cstdio>

#include "gz/LegacyControl.h"
#include "gz/ScriptVocab.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    auto* vocab = new cGZScriptVocab();
    vocab->AddRef();
    cRZString clsid("clsid"), CLSID("CLSID"), caption("caption"), missing("nope");

    // uint32 values: new -> true, the table holds the only reference
    CHECK(vocab->AddTranslation(clsid, 7u));
    CHECK(vocab->Size() == 1);
    cIGZVariant* got = nullptr;
    CHECK(vocab->GetTranslation(CLSID, &got) && got);                // case-insensitive
    std::uint32_t v = 0;
    got->GetValUint32(v);
    CHECK(v == 7);
    got->Release();                                                  // the caller's counted reference

    // a second Add of the same keyword (any case) changes nothing and answers false
    CHECK(!vocab->AddTranslation(CLSID, 9u));
    CHECK(vocab->GetTranslation(clsid, &got));
    got->GetValUint32(v);
    CHECK(v == 7 && vocab->Size() == 1);
    got->Release();

    // variant values: the table takes a reference on a new entry, leaves a duplicate alone
    auto* variant = new cRZVariant(3u);
    variant->AddRef();                                               // ours
    CHECK(vocab->AddTranslation(caption, variant));
    CHECK(variant->AddRef() == 3); variant->Release();               // ours + the table's
    auto* other = new cRZVariant(4u);
    other->AddRef();
    CHECK(!vocab->AddTranslation(caption, other));
    CHECK(other->AddRef() == 2); other->Release();                   // untouched by the failed add
    other->Release();

    // unknown names
    got = nullptr;
    CHECK(!vocab->GetTranslation(missing, &got) && got == nullptr);
    CHECK(!vocab->RemoveTranslation(missing));

    // remove releases the table's reference
    CHECK(vocab->RemoveTranslation(caption));
    CHECK(variant->AddRef() == 2); variant->Release();
    CHECK(vocab->Size() == 1);
    variant->Release();

    // QueryInterface: iid 1 and 0x1a2bdebb answer, others do not
    void* out = nullptr;
    CHECK(vocab->QueryInterface(1, &out) && out == static_cast<IScriptVocab*>(vocab));
    vocab->Release();
    CHECK(vocab->QueryInterface(kGZIID_cIGZScriptVocab, &out));
    vocab->Release();
    CHECK(!vocab->QueryInterface(0x1234, &out));

    // the LegacyControl.h view reaches the same slots the binary's vtable has (variant at 0x18, uint at 0x20)
    auto* legacyView = reinterpret_cast<IVocabulary*>(static_cast<IScriptVocab*>(vocab));
    cRZString viaLegacy("fromlegacy");
    CHECK(legacyView->AddTranslation(viaLegacy, 11u));
    auto* lv = new cRZVariant(12u);
    cRZString viaLegacy2("fromlegacy2");
    CHECK(legacyView->AddTranslation(viaLegacy2, static_cast<cIGZVariant*>(lv)));
    CHECK(vocab->GetTranslation(viaLegacy, &got));
    got->GetValUint32(v);
    CHECK(v == 11);
    got->Release();
    CHECK(vocab->GetTranslation(viaLegacy2, &got));
    got->GetValUint32(v);
    CHECK(v == 12);
    got->Release();

    // Clear drops everything (a variant we kept survives with our reference only)
    lv->AddRef();
    vocab->Clear();
    CHECK(vocab->Size() == 0 && lv->AddRef() == 2);
    lv->Release(); lv->Release();

    // Release at a count of 1 deletes the table
    CHECK(vocab->Release() == 0);

    std::printf(failures ? "gz_scriptvocab: %d FAILED\n" : "gz_scriptvocab: all passed\n", failures);
    return failures ? 1 : 0;
}
