#include "gz/CommandServer.h"

#include <cstdio>
#include <cstring>

#include "gz/StringCommandGenerator.h"
#include "rz/RZStringUtils.h"

// ---- cGZMessage2CommandDispatcherMessage ----

cGZMessage2CommandDispatcherMessage::~cGZMessage2CommandDispatcherMessage() {
    if (output_) output_->Release();
    if (input_) input_->Release();
}

bool cGZMessage2CommandDispatcherMessage::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 0x0b903cc5 || iid == 0x4b99446a) {
        *out = static_cast<cRZMessage2Standard*>(this);
        AddRef();
        return true;
    }
    return cRZMessage2Standard::QueryInterface(iid, out);
}

void cGZMessage2CommandDispatcherMessage::SetInput(cIGZCommandParameterSet* set) {
    if (input_ == set) return;
    if (set) set->AddRef();
    cIGZCommandParameterSet* old = input_;
    input_ = set;
    if (old) old->Release();
}

void cGZMessage2CommandDispatcherMessage::SetOutput(cIGZCommandParameterSet* set) {
    if (output_ == set) return;
    if (set) set->AddRef();
    cIGZCommandParameterSet* old = output_;
    output_ = set;
    if (old) old->Release();
}

// ---- cGZMessage2CommandDispatcher ----

namespace {
template <class T>
void Replace(T*& slot, T* value) {
    if (slot == value) return;
    if (value) value->AddRef();
    T* old = slot;
    slot = value;
    if (old) old->Release();
}
template <class T>
void Drop(T*& slot) {
    if (T* old = slot) {
        slot = nullptr;
        old->Release();
    }
}
}  // namespace

cGZMessage2CommandDispatcher::~cGZMessage2CommandDispatcher() {
    if (generator_) generator_->Release();
    if (output_) output_->Release();
    if (input_) input_->Release();
    if (message_) message_->Release();
}

bool cGZMessage2CommandDispatcher::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == 0x0b903cc4 || iid == kGZIID_cIGZCommandDispatcher) {
        *out = static_cast<cIGZCommandDispatcher*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZMessage2CommandDispatcher::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cGZMessage2CommandDispatcher::Init() {
    if (!server_.get()) {
        cRZSysServPtr<cIGZMessageServer2, 0x04fa845b, 0x652294c7> found;
        server_ = found.get();
    }
    if (!message_) {
        auto* message = new cGZMessage2CommandDispatcherMessage();
        message->AddRef();
        message_ = message;
    }
    if (!input_) Replace<cIGZCommandParameterSet>(input_, new cGZCommandParameterSet());
    if (!output_) Replace<cIGZCommandParameterSet>(output_, new cGZCommandParameterSet());
    if (!generator_) {
        auto* generator = new cGZStringCommandGenerator();
        Replace<cIGZStringCommandGenerator>(generator_, generator);
        generator_->Init();
    }
    return true;
}

bool cGZMessage2CommandDispatcher::Shutdown() {
    server_ = nullptr;
    if (message_) {
        message_->Release();
        message_ = nullptr;
    }
    Drop(input_);
    Drop(output_);
    if (generator_) {
        generator_->Shutdown();
        Drop(generator_);
    }
    return true;
}

std::uint32_t cGZMessage2CommandDispatcher::ExecuteCommand(std::uint32_t id, cIGZCommandParameterSet* in,
                                                           cIGZCommandParameterSet* out) {
    if (!server_.get()) return 2;
    if (!in) {
        input_->SetParameterCount(0);
        in = input_;
    }
    if (!out) {
        output_->SetParameterCount(0);
        out = output_;
    }
    message_->SetType(id);
    message_->SetVoid1(in);
    message_->SetInput(in);
    message_->SetVoid2(out);
    message_->SetOutput(out);
    server_->MessageSend(message_);
    if (cIGZVariant* status = out->GetParameter(kGZStatusParameterID)) return static_cast<std::uint32_t>(status->GetValSint32());
    out->SetStatusParameterValue(0);
    return 0;
}

// ---- cGZCommandServer ----

cGZCommandServer::~cGZCommandServer() {
    for (cIGZCommandGenerator* generator : generators_) generator->Release();
    if (generator_) generator_->Release();
    if (ownDispatcher_) ownDispatcher_->Release();
    if (dispatcher_) dispatcher_->Release();
}

bool cGZCommandServer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == 0xcb90253b || iid == kGZIID_cIGZCommandServer) {
        *out = static_cast<cIGZCommandServer*>(this);
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

std::uint32_t cGZCommandServer::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cGZCommandServer::Init() {
    if (!GetCommandDispatcher()) {
        auto* dispatcher = new cGZMessage2CommandDispatcher();
        if (ownDispatcher_ != dispatcher) {
            dispatcher->AddRef();   // the original sets the count to 1 directly
            cIGZCommandDispatcher* old = ownDispatcher_;
            ownDispatcher_ = dispatcher;
            if (old) old->Release();
        }
        ownDispatcher_->Init();
        SetCommandDispatcher(ownDispatcher_);
    }
    auto* generator = new cGZStringCommandGenerator();
    Replace<cIGZStringCommandGenerator>(generator_, generator);
    generator_->Init();
    RegisterCommandGenerator(generator_);
    return true;
}

bool cGZCommandServer::Shutdown() {
    commands_.clear();
    SetCommandDispatcher(nullptr);
    if (ownDispatcher_) {
        ownDispatcher_->Shutdown();
        Drop(ownDispatcher_);
    }
    if (generator_) {
        generator_->Shutdown();
        Drop(generator_);
    }
    for (cIGZCommandGenerator* generator : generators_) generator->Release();
    generators_.clear();
    return true;
}

void cGZCommandServer::CreateCommandParameterSet(cIGZCommandParameterSet** out) {
    auto* set = new cGZCommandParameterSet();
    *out = set;
    set->AddRef();
}

bool cGZCommandServer::NotifyCommandRegistration(const CommandInfo* info, bool registered, cIGZCommandGenerator* generator) {
    if (!info) {
        for (auto& entry : commands_) NotifyCommandRegistration(&entry.second, registered, generator);
    } else if (!generator) {
        for (cIGZCommandGenerator* each : generators_) NotifyCommandRegistration(info, registered, each);
    } else if (registered) {
        generator->CommandRegistered(info->id, info->name, info->help);
    } else {
        generator->CommandUnregistered(info->id, info->name, info->help);
    }
    return true;
}

bool cGZCommandServer::RegisterCommand(std::uint32_t id, const char* name, const char* help) {
    if (id == 0 || !name) return false;
    if (commands_.find(id) != commands_.end()) return false;
    CommandInfo& info = commands_[id];
    info.id = id;
    info.name.AssignFromCString(name);
    RegisterCommandParameterInfo(id, help);
    NotifyCommandRegistration(&info, true, nullptr);
    return true;
}

bool cGZCommandServer::RegisterCommands(const cGZCommandInfoEntry* entries, std::uint32_t count) {
    for (; count != 0 && entries->id != 0 && entries->name && *entries->name; ++entries, --count)
        RegisterCommand(entries->id, entries->name, entries->help);
    return true;
}

bool cGZCommandServer::RegisterCommandParameterInfo(std::uint32_t id, const char* text) {
    auto it = commands_.find(id);
    if (it == commands_.end()) return false;
    if (!text || !*text) {
        if (defaultHelp_) it->second.help.Sprintf("Format: %s\nOutput: <none>", it->second.name.ToChar());
        return true;
    }
    it->second.help.AssignFromCString(text);
    return true;
}

bool cGZCommandServer::UnregisterCommand(std::uint32_t id) {
    auto it = commands_.find(id);
    if (it == commands_.end()) return false;
    NotifyCommandRegistration(&it->second, false, nullptr);
    commands_.erase(it);
    return true;
}

bool cGZCommandServer::UnregisterCommands(const cGZCommandInfoEntry* entries, std::uint32_t count) {
    for (; count != 0 && entries->id != 0; ++entries, --count) UnregisterCommand(entries->id);
    return true;
}

bool cGZCommandServer::GetCommandInfo(std::uint32_t id, cIGZString& name, cIGZString* help) const {
    auto it = commands_.find(id);
    if (it == commands_.end()) return false;
    name.AssignFromIGZString(it->second.name);
    if (help) help->AssignFromIGZString(it->second.help);
    return true;
}

std::uint32_t cGZCommandServer::GetCommandID(const cIGZString& name) const {
    for (const auto& entry : commands_) {
        if (entry.second.name.CompareToString(name, false) == 0) return entry.second.id;
    }
    return 0;
}

std::uint32_t cGZCommandServer::GetCommandIDFromCommandString(const char* text, std::uint32_t length) const {
    if (!text) return 0;
    if (length == 0xffffffffu) length = static_cast<std::uint32_t>(std::strlen(text));
    cRZString token;
    unsigned count = 1;
    const bool parsed = RZParseDelimitedText(text, length, ' ', &token, count);
    if (parsed && count != 0) return GetCommandID(token);
    return 0;
}

void cGZCommandServer::EnumerateCommands(std::vector<std::uint32_t>& ids) const {
    for (const auto& entry : commands_) ids.push_back(entry.first);
}

bool cGZCommandServer::RegisterCommandGenerator(cIGZCommandGenerator* generator) {
    if (generator) generator->AddRef();
    if (generators_.find(generator) == generators_.end()) {
        if (generator) generator->AddRef();
        generators_.insert(generator);
        if (!commands_.empty()) NotifyCommandRegistration(nullptr, true, generator);
    }
    if (generator) generator->Release();
    return true;
}

bool cGZCommandServer::UnregisterCommandGenerator(cIGZCommandGenerator* generator) {
    if (generator) generator->AddRef();
    auto it = generators_.find(generator);
    if (it != generators_.end()) {
        generators_.erase(it);
        if (generator) generator->Release();   // the set's reference
    }
    if (generator) generator->Release();
    return true;
}

bool cGZCommandServer::SetCommandDispatcher(cIGZCommandDispatcher* dispatcher) {
    Replace<cIGZCommandDispatcher>(dispatcher_, dispatcher);
    return true;
}

std::uint32_t cGZCommandServer::ExecuteCommand(std::uint32_t id, cIGZCommandParameterSet* in, cIGZCommandParameterSet* out) {
    return dispatcher_ ? dispatcher_->ExecuteCommand(id, in, out) : 2;
}

std::uint32_t cGZCommandServer::ExecuteCommand(const char* text, std::uint32_t length, cIGZCommandParameterSet* out) {
    return generator_ ? generator_->ExecuteCommand(text, length, out) : 2;
}

std::uint32_t cGZCommandServer::ExecuteCommandFormatted(const char* format, cIGZCommandParameterSet* out, ...) {
    char buffer[0x1ff];
    va_list args;
    va_start(args, out);
    const int length = std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    if (length < 1) return 2;
    return ExecuteCommand(buffer, static_cast<std::uint32_t>(length), out);
}

bool cGZCommandServer::ConvertStringToCommand(const char* text, std::uint32_t length, std::uint32_t& id,
                                              cIGZCommandParameterSet*& params) {
    return generator_ ? generator_->ConvertStringToCommand(text, length, id, params) : false;
}

bool cGZCommandServer::ConvertCommandToString(std::uint32_t id, cIGZCommandParameterSet* params, cIGZString& out) {
    return generator_ ? generator_->ConvertCommandToString(id, params, out) : false;
}

bool cGZCommandServer::ConvertCommandOutputToString(cIGZCommandParameterSet* params, cIGZString& out) {
    return generator_ ? generator_->ConvertCommandOutputToString(params, out) : false;
}
