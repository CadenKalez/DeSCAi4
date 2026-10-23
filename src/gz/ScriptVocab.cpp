#include "gz/ScriptVocab.h"

#include "rz/cRZVariant.h"

cGZScriptVocab::~cGZScriptVocab() { Clear(); }

bool cGZScriptVocab::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x10054e698
    if (iid != 1 && iid != kGZIID_cIGZScriptVocab) return false;
    *out = static_cast<IScriptVocab*>(this);
    AddRef();
    return true;
}

std::uint32_t cGZScriptVocab::Release() {   // @ 0x10054e6c8
    if (refCount_ > 1) return --refCount_;
    delete this;   // the deleting destructor (vslot 0x48)
    return 0;
}

bool cGZScriptVocab::AddTranslation(const cIGZString& name, cIGZVariant* value) {   // @ 0x10054e156
    const bool inserted = table_.emplace(name.ToChar(), value).second;
    if (inserted) value->AddRef();   // vslot 8
    return inserted;
}

bool cGZScriptVocab::AddTranslation(const cIGZString& name, std::uint32_t value) {   // @ 0x10054deb4
    auto* variant = new cRZVariant(value);   // a count of 0 until the table takes it
    const bool inserted = table_.emplace(name.ToChar(), variant).second;
    if (inserted) variant->AddRef();
    else delete variant;                      // vslot 0x438: the unused variant is destroyed
    return inserted;
}

bool cGZScriptVocab::RemoveTranslation(const cIGZString& name) {   // @ 0x10054e3ce
    auto it = table_.find(name.ToChar());
    if (it == table_.end()) return false;
    it->second->Release();   // vslot 0x10
    table_.erase(it);
    return true;
}

bool cGZScriptVocab::GetTranslation(const cIGZString& name, cIGZVariant** out) {   // @ 0x10054e4ec
    auto it = table_.find(name.ToChar());
    if (it == table_.end()) return false;
    *out = it->second;
    it->second->AddRef();   // vslot 8: the caller releases
    return true;
}

void cGZScriptVocab::Clear() {   // @ 0x10054e60e
    for (auto& entry : table_) entry.second->Release();
    table_.clear();
}
