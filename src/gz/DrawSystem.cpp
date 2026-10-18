#include "gz/DrawSystem.h"

#include "gz/DrawContextAsBuffer.h"

namespace {
IDrawSystemHost* g_host = nullptr;
cGZDrawContextRenderToBufferFactory::EngineCreator g_engineCreator = nullptr;

// DAT_100a28130: the bits-per-pixel half of the colour type for types 2..9 (8, 16 x5, 32 x2); anything else gets none.
constexpr std::uint32_t kBitsForType[8] = {8, 16, 16, 16, 16, 16, 32, 32};
}  // namespace

void SetDrawSystemHost(IDrawSystemHost* host) { g_host = host; }
IDrawSystemHost* GetDrawSystemHost() { return g_host; }

void cGZDrawContextRenderToBufferFactory::SetEngineCreator(EngineCreator creator) { g_engineCreator = creator; }

bool cGZDrawContextRenderToBufferFactory::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != kGZIID_cIGZDrawContextEngineFactory && iid != kGZIID_cIGZUnknown) return false;
    *out = static_cast<cIGZDrawContextEngineFactory*>(this);
    AddRef();
    return true;
}

bool cGZDrawContextRenderToBufferFactory::CreateEngine(cIGZUnknown* target, gz::cIGZDrawEngine** out) {
    // The original hands `out` itself to the buffer query, so on success the buffer reference lands in *out and
    // is then overwritten by the engine (the reference is never released).
    if (!target->QueryInterface(kGZIID_cIGZBuffer, reinterpret_cast<void**>(out))) return false;
    if (!g_engineCreator) return false;
    gz::cIGZDrawEngine* engine = g_engineCreator();
    *out = engine;
    if (engine) engine->AddRef();
    return engine != nullptr;
}

cGZDrawSystem::~cGZDrawSystem() {
    for (cIGZDrawContextEngineFactory* factory : engines_) {
        if (factory) factory->Release();
    }
}

bool cGZDrawSystem::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZDrawSystem) {
        *out = static_cast<cIGZDrawSystem*>(this);
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

bool cGZDrawSystem::Init() {
    auto* factory = new cGZDrawContextRenderToBufferFactory();
    factory->AddRef();
    engines_.push_back(factory);   // the list keeps the reference taken above
    if (g_host) g_host->AddTickClient(static_cast<cIGZSystemService*>(this));
    return true;
}

bool cGZDrawSystem::Shutdown() {
    if (g_host) g_host->RemoveTickClient(static_cast<cIGZSystemService*>(this));
    return true;
}

void cGZDrawSystem::AddEngine(cIGZDrawContextEngineFactory* factory) {
    if (factory) factory->AddRef();
    engines_.push_front(factory);   // later factories take precedence
}

bool cGZDrawSystem::CreateEngine(cIGZUnknown* target, gz::cIGZDrawEngine** out) {
    for (cIGZDrawContextEngineFactory* factory : engines_) {
        if (factory->CreateEngine(target, out)) return true;
    }
    return false;
}

void cGZDrawSystem::RegisterBufferCache(cIGZBufferHardwareCache* cache) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    caches_.push_back(cache);
}

void cGZDrawSystem::UnregisterBufferCache(cIGZBufferHardwareCache* cache) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (auto it = caches_.begin(); it != caches_.end(); ++it) {
        if (*it == cache) {
            caches_.erase(it);
            break;
        }
    }
}

bool cGZDrawSystem::OnTick(std::uint32_t) {
    if (clock_ % 5 == 0) FlushOldBufferCaches(5);
    ++clock_;
    return true;
}

void cGZDrawSystem::FlushOldBufferCaches(std::uint32_t) {
    // The age argument is ignored: anything resident and unused for more than four clock steps goes.
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (cIGZBufferHardwareCache* cache : caches_) {
        if (cache->IsResident() && clock_ - cache->GetLastUsedClock() > 4) cache->Flush();
    }
}

void cGZDrawSystem::FlushBufferCaches() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (cIGZBufferHardwareCache* cache : caches_) cache->Flush();
}

bool cGZDrawSystem::CreatePrivateBuffer(cIGZBuffer** out, int width, int height, std::int32_t colorType) {
    cIGZBuffer* buffer = nullptr;
    if (!g_host || !g_host->CreateBuffer(&buffer)) return false;
    bool ok = false;
    cGZBufferColorType type;
    type.type = colorType;
    const std::uint32_t index = static_cast<std::uint32_t>(colorType) - 2;
    type.bitsPerPixel = index < 8 ? kBitsForType[index] : 0;
    if (buffer->Init(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), type)) {
        auto* asBuffer = new gz::cGZDrawContextAsBuffer();
        asBuffer->AddRef();
        auto* context = new gz::cGZDrawContext();
        context->AddRef();
        context->SetTarget(buffer);
        asBuffer->Init(context, buffer);
        asBuffer->AddRef();
        *out = asBuffer;
        context->Release();
        asBuffer->Release();
        ok = true;
    }
    buffer->Release();
    return ok;
}
