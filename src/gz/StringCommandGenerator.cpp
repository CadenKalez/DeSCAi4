#include "gz/StringCommandGenerator.h"

#include <cstring>

#include "rz/RZStringUtils.h"

cGZStringCommandGenerator::cGZStringCommandGenerator() {
    server_ = nullptr;   // the service pointer starts empty: Init looks it up
    parameters_ = new cGZCommandParameterSet();
    parameters_->AddRef();
}

cGZStringCommandGenerator::~cGZStringCommandGenerator() {
    if (parameters_) parameters_->Release();
}

bool cGZStringCommandGenerator::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZCommandGenerator || iid == kGZIID_cIGZStringCommandGenerator) {
        *out = static_cast<cIGZStringCommandGenerator*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZStringCommandGenerator::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cGZStringCommandGenerator::Init() {
    if (!server_.get()) {
        cRZSysServPtr<cIGZCommandServer, kGZSID_cGZCommandServer, kGZIID_cIGZCommandServer> found;
        server_ = found.get();
    }
    return true;
}

bool cGZStringCommandGenerator::Shutdown() {
    server_ = nullptr;
    if (parameters_) {
        cIGZCommandParameterSet* set = parameters_;
        parameters_ = nullptr;
        set->Release();
    }
    return true;
}

std::uint32_t cGZStringCommandGenerator::ExecuteCommand(const char* text, std::uint32_t length,
                                                        cIGZCommandParameterSet* output) {
    cIGZCommandParameterSet* input = nullptr;
    if (!server_.get()) return 2;
    std::uint32_t id = 0;
    std::uint32_t status = 2;
    if (ConvertStringToCommand(text, length, id, input)) status = server_->ExecuteCommand(id, input, output);
    if (input) input->Release();
    return status;
}

bool cGZStringCommandGenerator::ConvertStringToCommand(const char* text, std::uint32_t length, std::uint32_t& id,
                                                       cIGZCommandParameterSet*& params) {
    if (!server_.get()) return false;
    if (length == 0xffffffffu) length = static_cast<std::uint32_t>(std::strlen(text));
    unsigned count = 1;
    tokens_.resize(1);
    bool fits;
    do {
        const std::size_t size = tokens_.size();
        if (size > 0x1ff) break;
        count = static_cast<unsigned>(size);
        fits = RZParseDelimitedText(text, length, ' ', tokens_.data(), count);
        tokens_.resize(tokens_.size() * 2);   // the vector grows even when the parse fit
    } while (!fits);
    if (count == 0) return false;
    id = server_->GetCommandID(tokens_[0]);
    if (id == 0) return false;
    auto* set = new cGZCommandParameterSet();
    params = set;
    set->AddRef();
    for (unsigned i = 1; i < count; ++i) {
        cRZVariant value(static_cast<const cIGZString&>(tokens_[i]));
        set->SetParameter(i - 1, value);
    }
    return true;
}

bool cGZStringCommandGenerator::ConvertCommandToString(std::uint32_t id, cIGZCommandParameterSet* params,
                                                       cIGZString& out) {
    out.Erase(0, 0xffffffffu);
    if (!server_.get()) return false;
    cRZString text;
    if (!server_->GetCommandInfo(id, text, nullptr)) return false;
    ConvertStringToArgumentString(text, text);
    out.AppendString(text);
    if (params) {
        for (std::uint32_t pid = params->GetFirstParameterID(); pid != 0xffffffffu; pid = params->GetNextParameterID(pid)) {
            cIGZVariant* value = params->GetParameter(pid);
            value->GetValString(text);
            ConvertStringToArgumentString(text, text);
            out.AppendChars(" ", 1);
            out.AppendString(text);
        }
    }
    return true;
}

void cGZStringCommandGenerator::ConvertStringToArgumentString(const cIGZString& in, cIGZString& out) {
    if (&out != &in) out.AssignFromIGZString(in);
    if (in.FindChars(" ", 0, true) == static_cast<std::size_t>(-1) &&
        in.FindChars("\t", 0, true) == static_cast<std::size_t>(-1)) {
        return;
    }
    out.InsertChars(0, "\"", 1);
    out.AppendChars("\"", 1);
}

bool cGZStringCommandGenerator::ConvertCommandOutputToString(cIGZCommandParameterSet* params, cIGZString& out) {
    cRZString text;
    out.Resize(0);
    const std::uint32_t first = params->GetFirstParameterID();
    for (std::uint32_t pid = first; pid != 0xffffffffu; pid = params->GetNextParameterID(pid)) {
        cIGZVariant* value = params->GetParameter(pid);
        value->GetValString(text);
        ConvertStringToArgumentString(text, text);
        if (pid != first) out.AppendChars(" ", 1);
        out.AppendString(text);
    }
    return true;
}
