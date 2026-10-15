#include "gz/CommandParameterSet.h"

#include <cstring>

#include "rz/cRZString.h"

bool cGZCommandParameterSet::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 1 || iid == kGZIID_cIGZCommandParameterSet || iid == kGZIID_cIGZCommandParameterSetAlt) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZCommandParameterSet::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

void cGZCommandParameterSet::SetParameterCount(std::uint32_t count) {
    while (parameters_.size() < count) AppendParameter(cRZVariant());
    while (count < parameters_.size()) parameters_.erase(std::prev(parameters_.end()));
}

cIGZVariant& cGZCommandParameterSet::operator[](std::uint32_t id) {
    auto it = parameters_.find(id);
    return it == parameters_.end() ? static_cast<cIGZVariant&>(null_) : it->second;
}

cIGZVariant* cGZCommandParameterSet::GetParameter(std::uint32_t id) {
    auto it = parameters_.find(id);
    return it == parameters_.end() ? nullptr : &it->second;
}

void cGZCommandParameterSet::SetParameter(std::uint32_t id, const cIGZVariant& value) {
    parameters_[id] = value;
}

void cGZCommandParameterSet::AppendParameter(const cIGZVariant& value) {
    const std::uint32_t id = parameters_.empty() ? 0 : std::prev(parameters_.end())->first + 1;
    SetParameter(id, value);
}

void cGZCommandParameterSet::RemoveParameter(std::uint32_t id) { parameters_.erase(id); }

void cGZCommandParameterSet::SetStatusParameterValue(std::int32_t value) {
    cRZVariant status;
    status.SetValSint32(value);
    SetParameter(kGZStatusParameterID, status);
}

bool cGZCommandParameterSet::CompareParameterString(std::uint32_t id, const char* text, bool caseSensitive) {
    if (id == kGZAnyParameterID) {
        for (auto& entry : parameters_)
            if (CompareParameterString(entry.first, text, caseSensitive)) return true;
        return false;
    }
    cIGZVariant* parameter = GetParameter(id);
    if (!parameter) return false;
    cRZString value;
    if (!parameter->GetValString(value)) return false;
    // the text must occur in the value and the lengths agree, i.e. equality up to case
    if (value.FindChars(text, 0, caseSensitive) == static_cast<std::size_t>(-1)) return false;
    return value.Strlen() == std::strlen(text);
}

bool cGZCommandParameterSet::ValidateParameter(std::uint32_t id, std::uint32_t type) {
    return type == 0xff && GetParameter(id) != nullptr;
}

std::uint32_t cGZCommandParameterSet::GetFirstParameterID() const {
    return parameters_.empty() ? kGZNoParameterID : parameters_.begin()->first;
}

std::uint32_t cGZCommandParameterSet::GetNextParameterID(std::uint32_t id) const {
    auto it = parameters_.find(id);
    if (it == parameters_.end()) return kGZNoParameterID;
    ++it;
    return it == parameters_.end() ? kGZNoParameterID : it->first;
}
