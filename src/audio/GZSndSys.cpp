#include "audio/GZSndSys.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include "audio/AudioServices.h"
#include "audio/GZSndCaches.h"
#include "audio/GZSndEventStd.h"
#include "audio/interfaces/cIGZSnd.h"
#include "gz/cGZMessageQueue2.h"

namespace {

// RAII for the recursive framework critical sections.
class Guard {
public:
    explicit Guard(cRZCriticalSection& section) : section_(section) { section_.Lock(); }
    ~Guard() { section_.Unlock(); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
private:
    cRZCriticalSection& section_;
};

// UpdateTarget @ 0x100493980: ticks a target and keeps going.
bool TickTarget(cIGZSndUpdateTarget* target, std::uint32_t elapsed, void*) {
    target->Update(static_cast<std::int32_t>(elapsed));
    return true;
}

// EnumFunc_FindPlayingGZSnd @ 0x1004939c0: stops at the first target that is a sound with the id.
struct FindContext {
    cIGZSnd* found = nullptr;
};
bool FindSndFunction(cIGZSndUpdateTarget* target, std::uint32_t id, void* data) {
    void* raw = nullptr;
    if (target->QueryInterface(kGZIID_cIGZSnd, &raw)) {
        auto* snd = static_cast<cIGZSnd*>(raw);
        const bool match = snd->GetID() == id;
        snd->Release();
        if (match) {
            static_cast<FindContext*>(data)->found = snd;   // not referenced (the target list keeps it alive)
            return false;
        }
    }
    return true;
}

std::string NormalizeExtension(const cIGZString& extension) {
    std::string text(extension.Data(), extension.Strlen());
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return std::string();
    const auto last = text.find_last_not_of(" \t\r\n");
    text = text.substr(first, last - first + 1);
    if (!text.empty() && text[0] == '.') text.erase(0, 1);
    return text;
}

constexpr std::uint32_t kIidMessageTarget2 = 0x452294aa;
constexpr std::uint32_t kIidSystemService = 0x287fb697;

void TimerCallback(void* context) {
    static std::uint32_t lastCall = 0;
    auto* system = static_cast<cGZSndSys*>(context);
    const std::uint32_t now = system->GetElapsedTime();
    std::uint32_t delta = now - lastCall;
    if (now < lastCall) delta = 0;
    system->HandleTimerCallback(delta);
    lastCall = now;
}

}  // namespace

cGZSndSys::cGZSndSys() {
    // The constructor asks the file system service for the data directory (see Init).
    if (!Audio::FileSystemDataDirectory().empty()) {
        cRZString directory(Audio::FileSystemDataDirectory().c_str());
        SetDataDirectory(directory);
    }
}

// ~cGZSndSys @ 0x100487b50: Shutdown runs first.
cGZSndSys::~cGZSndSys() {
    Shutdown();
    delete callbackTimer_;
    delete clock_;
}

bool cGZSndSys::QueryInterface(std::uint32_t iid, void** out) {
    void* result = nullptr;
    switch (iid) {
        case kGZIID_cIGZUnknown:
        case kGZIID_cIGZSndSys: result = static_cast<cIGZSndSys*>(this); break;
        case kIidMessageTarget2: result = static_cast<cIGZMessageTarget2*>(this); break;
        case 0x406077d2: result = static_cast<cIGZSnd3DListener*>(this); break;
        case 0x5b30e7b3: result = static_cast<cIGZSnd3DListenerEx*>(this); break;
        case kIidSystemService: result = static_cast<cIGZSystemService*>(this); break;
        case kGZIID_cIGZSndEaRendererEffects: result = static_cast<cIGZSndEaRendererEffects*>(this); break;
        default: return false;
    }
    *out = result;
    AddRef();
    return true;
}

// cGZSndSys::Release() @ 0x100486130: destroys below 2.
std::uint32_t cGZSndSys::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

std::uint32_t cGZSndSys::GetElapsedTime() const { return clock_ ? clock_->GetElapsedTimeU32() : 0; }

void cGZSndSys::SetDataDirectory(const cIGZString& directory) {
    dataDirectoryString_.Copy(directory);
    dataDirectorySet_ = true;
}

// cGZSndSys::Init() @ 0x1004881f0
bool cGZSndSys::Init() {
    Guard guard(criticalSection_);
    if (!dataDirectorySet_ && !Audio::FileSystemDataDirectory().empty()) {
        cRZString directory(Audio::FileSystemDataDirectory().c_str());
        SetDataDirectory(directory);
    }
    if (cIGZMessageServer2* server = Audio::MessageServer()) {
        server->AddNotification(static_cast<cIGZMessageTarget2*>(this), kMessageTypeSndEvent);
    }
    if (!GetEnabledFlag()) return true;
    if (!GetInitFlag()) {
        queue_ = new cGZMessageQueue2();
        propertyCache_ = cGZSndPropertyCache::Create();
        instanceCache_ = cGZSndInstanceCache::Create();
        clock_ = new cRZTimer(cRZTimer::kMilliseconds);
        clock_->Start();
        slowTimer_.Start();
        callbackTimer_ = new cRZCallbackTimer();
        callbackTimer_->SetResolution(kFastIntervalMs);
        callbackTimer_->SetCallbackFunction(&TimerCallback, this);
        SetInitFlag(callbackTimer_->StartPeriodic(kFastIntervalMs, nullptr));
    }
    return true;
}

// cGZSndSys::Shutdown() @ 0x10048a0e0: stops the timers, kills every sound that is an update
// target, then drops targets, handlers, renderers' references to this system's caches and queue.
bool cGZSndSys::Shutdown() {
    shuttingDown_ = true;
    {
        Guard guard(criticalSection_);
        if (callbackTimer_) callbackTimer_->Stop();
        if (clock_) clock_->Stop();
    }
    Guard guard(criticalSection_);
    if (cIGZMessageServer2* server = Audio::MessageServer()) {
        server->RemoveNotification(static_cast<cIGZMessageTarget2*>(this), kMessageTypeSndEvent);
    }
    if (callbackTimer_) {
        callbackTimer_->Stop();
        delete callbackTimer_;
        callbackTimer_ = nullptr;
    }
    if (clock_) {
        clock_->Stop();
        delete clock_;
        clock_ = nullptr;
    }
    {
        Guard handlerGuard(handlerLock_);
        ++handlerGuard_;
        handlers_.clear();
        pendingHandlerAdd_.clear();
        pendingHandlerRemove_.clear();
        --handlerGuard_;
    }
    {
        Guard targetGuard(targetLock_);
        ++targetGuard_;
        std::vector<cRZAutoRefCount<cIGZSnd>> sounds;
        for (TargetList* list : {&fastTargets_, &slowTargets_}) {
            for (auto& target : *list) {
                void* raw = nullptr;
                if (target->QueryInterface(kGZIID_cIGZSnd, &raw)) {
                    cRZAutoRefCount<cIGZSnd> snd;
                    snd = static_cast<cIGZSnd*>(raw);
                    static_cast<cIGZSnd*>(raw)->Release();
                    sounds.push_back(snd);
                }
            }
        }
        for (auto& snd : sounds) snd->Kill();
        --targetGuard_;
        fastTargets_.clear();
        slowTargets_.clear();
        pendingFast_.clear();
        pendingSlow_.clear();
        pendingRemove_.clear();
    }
    SetInitFlag(false);
    mainlineRelease_.clear();
    extensionMap_.clear();
    propertyCache_ = nullptr;
    instanceCache_ = nullptr;
    queue_ = nullptr;
    return true;
}

// ============================================================================================
// update targets
// ============================================================================================

// @ 0x100490830. While a loop runs (guard set) new targets wait in the pending lists.
bool cGZSndSys::RegisterTarget(cIGZSndUpdateTarget* target, SndCallbackInterval interval) {
    Guard guard(targetLock_);
    cRZAutoRefCount<cIGZSndUpdateTarget> ref(target);
    if (targetGuard_ == 0) {
        UnregisterTarget(target);
        (interval == kFastIntervalMs ? fastTargets_ : slowTargets_).push_back(ref);
    } else {
        (interval == kFastIntervalMs ? pendingFast_ : pendingSlow_).push_back(ref);
    }
    return true;
}

// @ 0x100491410. During a loop the removal is queued.
bool cGZSndSys::UnregisterTarget(cIGZSndUpdateTarget* target) {
    if (shuttingDown_) return true;
    Guard guard(targetLock_);
    if (targetGuard_ == 0) {
        for (TargetList* list : {&fastTargets_, &slowTargets_}) {
            for (auto it = list->begin(); it != list->end(); ++it) {
                if (it->get() == target) {
                    cRZAutoRefCount<cIGZSndUpdateTarget> keepAlive = *it;
                    list->erase(it);
                    break;
                }
            }
        }
    } else {
        pendingRemove_.push_back(cRZAutoRefCount<cIGZSndUpdateTarget>(target));
    }
    return true;
}

// @ 0x100493de0: the fast list every call, the slow list when more than 250 ms passed since its
// last run, then queued registrations / removals.
bool cGZSndSys::UpdateTargets(UpdateTargetFunction function, std::uint32_t arg, void* userData) {
    if (!updatesEnabled_) return true;
    {
        Guard guard(targetLock_);
        ++targetGuard_;
        {
            // iterate over a copy of the pointers so nested loops stay valid
            std::vector<cRZAutoRefCount<cIGZSndUpdateTarget>> fast(fastTargets_.begin(), fastTargets_.end());
            for (auto& target : fast) {
                if (!function(target.get(), arg, userData)) break;
            }
        }
        if (slowTimer_.GetElapsedTimeU32() > kSlowIntervalMs) {
            slowTimer_.Restart();
            std::vector<cRZAutoRefCount<cIGZSndUpdateTarget>> slow(slowTargets_.begin(), slowTargets_.end());
            for (auto& target : slow) {
                if (!function(target.get(), arg, userData)) break;
            }
        }
        --targetGuard_;
        if (targetGuard_ == 0) {
            while (!pendingRemove_.empty()) {
                cRZAutoRefCount<cIGZSndUpdateTarget> target = pendingRemove_.front();
                pendingRemove_.pop_front();
                UnregisterTarget(target.get());
            }
            while (!pendingFast_.empty()) {
                cRZAutoRefCount<cIGZSndUpdateTarget> target = pendingFast_.front();
                pendingFast_.pop_front();
                RegisterTarget(target.get(), kFastIntervalMs);
            }
            while (!pendingSlow_.empty()) {
                cRZAutoRefCount<cIGZSndUpdateTarget> target = pendingSlow_.front();
                pendingSlow_.pop_front();
                RegisterTarget(target.get(), kSlowIntervalMs);
            }
        }
    }
    return true;
}

// @ 0x100494d00: both lists, no timing, stops at the first false.
bool cGZSndSys::EnumerateTargets(UpdateTargetFunction function, std::uint32_t arg, void* userData) {
    bool keepGoing = true;
    std::vector<cRZAutoRefCount<cIGZSndUpdateTarget>> all(fastTargets_.begin(), fastTargets_.end());
    all.insert(all.end(), slowTargets_.begin(), slowTargets_.end());
    // The original stops each list at its own false but continues with the next list (and
    // returns the last callback result).
    for (TargetList* list : {&fastTargets_, &slowTargets_}) {
        std::vector<cRZAutoRefCount<cIGZSndUpdateTarget>> copy(list->begin(), list->end());
        for (auto& target : copy) {
            keepGoing = function(target.get(), arg, userData);
            if (!keepGoing) break;
        }
    }
    return keepGoing;
}

bool cGZSndSys::FindPlayingGZSnd(std::uint32_t id, std::uint32_t iid, void** out) {
    FindContext context;
    EnumerateTargets(&FindSndFunction, id, &context);
    return context.found ? context.found->QueryInterface(iid, out) : false;
}

bool cGZSndSys::IsSndPlaying(std::uint32_t id) {
    FindContext context;
    EnumerateTargets(&FindSndFunction, id, &context);
    return context.found != nullptr;
}

// Panic @ 0x100488a00: kills every target that is a sound.
void cGZSndSys::Panic() {
    Guard guard(criticalSection_);
    Guard targetGuard(targetLock_);
    ++targetGuard_;
    std::vector<cRZAutoRefCount<cIGZSnd>> sounds;
    for (TargetList* list : {&fastTargets_, &slowTargets_}) {
        for (auto& target : *list) {
            void* raw = nullptr;
            if (target->QueryInterface(kGZIID_cIGZSnd, &raw)) {
                cRZAutoRefCount<cIGZSnd> snd;
                snd = static_cast<cIGZSnd*>(raw);
                static_cast<cIGZSnd*>(raw)->Release();
                sounds.push_back(snd);
            }
        }
    }
    for (auto& snd : sounds) snd->Kill();
    --targetGuard_;
}

// ============================================================================================
// events
// ============================================================================================

bool cGZSndSys::RegisterEventHandler(cIGZSndEventHandler* handler) {
    Guard guard(handlerLock_);
    cRZAutoRefCount<cIGZSndEventHandler> ref(handler);
    if (handlerGuard_ == 0) {
        UnregisterEventHandler(handler);
        handlers_.push_back(ref);
    } else {
        pendingHandlerAdd_.push_back(ref);
    }
    return true;
}

bool cGZSndSys::UnregisterEventHandler(cIGZSndEventHandler* handler) {
    Guard guard(handlerLock_);
    if (handlerGuard_ == 0) {
        for (auto it = handlers_.begin(); it != handlers_.end(); ++it) {
            if (it->get() == handler) {
                cRZAutoRefCount<cIGZSndEventHandler> keepAlive = *it;
                handlers_.erase(it);
                break;
            }
        }
    } else {
        pendingHandlerRemove_.push_back(cRZAutoRefCount<cIGZSndEventHandler>(handler));
    }
    return true;
}

// @ 0x100496800: the first handler that consumes the event ends the dispatch.
bool cGZSndSys::DoEvent(cIGZSndEventStd* event) {
    bool consumed = false;
    {
        Guard guard(handlerLock_);
        ++handlerGuard_;
        std::vector<cRZAutoRefCount<cIGZSndEventHandler>> snapshot(handlers_.begin(), handlers_.end());
        for (auto& handler : snapshot) {
            if (handler->DoEvent(event)) {
                consumed = true;
                break;
            }
        }
        --handlerGuard_;
        if (handlerGuard_ == 0) {
            while (!pendingHandlerAdd_.empty()) {
                cRZAutoRefCount<cIGZSndEventHandler> handler = pendingHandlerAdd_.front();
                pendingHandlerAdd_.pop_front();
                RegisterEventHandler(handler.get());
            }
            while (!pendingHandlerRemove_.empty()) {
                cRZAutoRefCount<cIGZSndEventHandler> handler = pendingHandlerRemove_.front();
                pendingHandlerRemove_.pop_front();
                UnregisterEventHandler(handler.get());
            }
        }
    }
    return consumed;
}

bool cGZSndSys::EventCreate(std::uint32_t iid, void** out) {
    auto* event = new cGZSndEventStd();
    event->AddRef();
    const bool ok = event->QueryInterface(iid, out);
    event->Release();
    return ok;
}

bool cGZSndSys::EventCreateWithData(std::uint32_t eventId, std::uint32_t iid, void** out, std::uint32_t d1,
                                    std::uint32_t d2, std::uint32_t d3, std::uint32_t d4) {
    auto* event = new cGZSndEventStd(eventId, d1, d2, d3, d4);
    event->AddRef();
    const bool ok = event->QueryInterface(iid, out);
    event->Release();
    return ok;
}

namespace {
// Creates an empty event of class `eventId` and returns it as cIGZSndEventStd (AddRef'd).
cIGZSndEventStd* NewEvent(cGZSndSys& system, std::uint32_t eventId) {
    void* raw = nullptr;
    if (!system.EventCreateWithData(eventId, kGZCLSID_cGZSndEventStd, &raw, 0, 0, 0, 0)) return nullptr;
    return static_cast<cIGZSndEventStd*>(raw);
}
}  // namespace

bool cGZSndSys::DoEventIds(std::uint32_t id, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    cIGZSndEventStd* event = NewEvent(*this, id);
    if (!event) return false;
    cIGZMessage2Standard* message = event->AsMessage2Standard();
    message->SetData1(a); message->SetData2(b); message->SetData3(c); message->SetData4(d);
    const bool result = DoEvent(event);
    event->Release();
    return result;
}

bool cGZSndSys::DoEventUnknown(std::uint32_t id, cIGZUnknown* u, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    cIGZSndEventStd* event = NewEvent(*this, id);
    if (!event) return false;
    cIGZMessage2Standard* message = event->AsMessage2Standard();
    message->SetIGZUnknown(u);
    message->SetData1(a); message->SetData2(b); message->SetData3(c); message->SetData4(d);
    const bool result = DoEvent(event);
    event->Release();
    return result;
}

// The string variants only carry the string; the numeric arguments are dropped (as in the binary).
bool cGZSndSys::DoEventString(std::uint32_t id, cIGZString* s, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {
    cIGZSndEventStd* event = NewEvent(*this, id);
    if (!event) return false;
    event->AsMessage2Standard()->SetString(s);
    const bool result = DoEvent(event);
    event->Release();
    return result;
}

bool cGZSndSys::PostEvent(cIGZSndEventStd* event) {
    if (!queue_) return true;   // original dereferences a null queue
    queue_->PushBack(event->AsMessage2Standard());
    return true;
}

bool cGZSndSys::PostEventIds(std::uint32_t id, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    cIGZSndEventStd* event = NewEvent(*this, id);
    if (!event) return false;
    cIGZMessage2Standard* message = event->AsMessage2Standard();
    message->SetData1(a); message->SetData2(b); message->SetData3(c); message->SetData4(d);
    const bool result = PostEvent(event);
    event->Release();
    return result;
}

bool cGZSndSys::PostEventUnknown(std::uint32_t id, cIGZUnknown* u, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    cIGZSndEventStd* event = NewEvent(*this, id);
    if (!event) return false;
    cIGZMessage2Standard* message = event->AsMessage2Standard();
    message->SetIGZUnknown(u);
    message->SetData1(a); message->SetData2(b); message->SetData3(c); message->SetData4(d);
    const bool result = PostEvent(event);
    event->Release();
    return result;
}

bool cGZSndSys::PostEventString(std::uint32_t id, cIGZString* s, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {
    cIGZSndEventStd* event = NewEvent(*this, id);
    if (!event) return false;
    event->AsMessage2Standard()->SetString(s);
    const bool result = PostEvent(event);
    event->Release();
    return result;
}

// @ 0x100495450: events arriving as framework messages (type 0xdb53383b) are executed when a
// renderer exists.
bool cGZSndSys::DoMessage(cIGZMessage2* message) {
    if (message->GetType() != kMessageTypeSndEvent) return false;
    void* raw = nullptr;
    if (!message->QueryInterface(kGZCLSID_cGZSndEventStd, &raw) || !raw) return false;
    auto* event = static_cast<cIGZSndEventStd*>(raw);
    bool result = false;
    if (NumRenderers() != 0) result = DoEvent(event);
    event->Release();
    return result;
}

// @ 0x100493c10: drains the event queue for up to 26 ms, then ticks the update targets.
bool cGZSndSys::Update(std::uint32_t elapsedMs) {
    if (!GetInitFlag()) return false;
    const std::uint32_t start = GetElapsedTime();
    cIGZMessage2* message = nullptr;
    while (queue_ && queue_->GetFrontMessage(&message)) {
        DoMessage(message);
        message->Release();
        if (GetElapsedTime() - start >= 0x1a) break;
    }
    UpdateTargets(&TickTarget, elapsedMs, nullptr);
    return true;
}

// ============================================================================================
// renderers and the forwarded listener settings
// ============================================================================================

bool cGZSndSys::RegisterRenderer(cIGZSndRenderer* renderer) {
    Guard guard(rendererLock_);
    if (!IsRegisteredRenderer(renderer)) renderers_.push_back(cRZAutoRefCount<cIGZSndRenderer>(renderer));
    return true;
}

bool cGZSndSys::UnregisterRenderer(cIGZSndRenderer* renderer) {
    Guard guard(rendererLock_);
    for (auto it = renderers_.begin(); it != renderers_.end(); ++it) {
        if (it->get() == renderer) {
            cRZAutoRefCount<cIGZSndRenderer> keepAlive = *it;
            renderers_.erase(it);
            return true;
        }
    }
    return false;
}

bool cGZSndSys::IsRegisteredRenderer(cIGZSndRenderer* renderer) {
    Guard guard(rendererLock_);
    for (auto& entry : renderers_) {
        if (entry.get() == renderer) return true;
    }
    return false;
}

template <class T, class F>
void cGZSndSys::ForEachRendererInterface(std::uint32_t iid, F&& apply) {
    Guard guard(rendererLock_);
    std::vector<cRZAutoRefCount<cIGZSndRenderer>> snapshot(renderers_.begin(), renderers_.end());
    for (auto& renderer : snapshot) {
        void* raw = nullptr;
        if (renderer->QueryInterface(iid, &raw) && raw) {
            T* iface = static_cast<T*>(raw);
            apply(*iface);
            iface->Release();
        }
    }
}

// SetMasterVolume @ 0x10048f930: the argument is never stored; renderers receive the stored value.
bool cGZSndSys::SetMasterVolume(std::int32_t) {
    Guard guard(rendererLock_);
    std::vector<cRZAutoRefCount<cIGZSndRenderer>> snapshot(renderers_.begin(), renderers_.end());
    for (auto& renderer : snapshot) renderer->SetMasterVolume(masterVolume_);
    return true;
}

bool cGZSndSys::CommitDeferredSettings() {
    ForEachRendererInterface<cIGZSnd3DListener>(0x406077d2, [](cIGZSnd3DListener& l) { l.CommitDeferredSettings(); });
    return true;
}

bool cGZSndSys::GetPosition(float* x, float* y, float* z) const {
    *x = position_[0]; *y = position_[1]; *z = position_[2];
    return true;
}

bool cGZSndSys::SetPosition(float x, float y, float z) {
    position_[0] = x; position_[1] = y; position_[2] = z;
    ForEachRendererInterface<cIGZSnd3DListener>(0x406077d2, [&](cIGZSnd3DListener& l) { l.SetPosition(x, y, z); });
    return true;
}

bool cGZSndSys::GetOrientation(float* fx, float* fy, float* fz, float* ux, float* uy, float* uz) const {
    *fx = orientation_[0]; *fy = orientation_[1]; *fz = orientation_[2];
    *ux = orientation_[3]; *uy = orientation_[4]; *uz = orientation_[5];
    return true;
}

bool cGZSndSys::SetOrientation(float fx, float fy, float fz, float ux, float uy, float uz) {
    orientation_[0] = fx; orientation_[1] = fy; orientation_[2] = fz;
    orientation_[3] = ux; orientation_[4] = uy; orientation_[5] = uz;
    ForEachRendererInterface<cIGZSnd3DListener>(0x406077d2, [&](cIGZSnd3DListener& l) { l.SetOrientation(fx, fy, fz, ux, uy, uz); });
    return true;
}

bool cGZSndSys::GetVelocity(float* x, float* y, float* z) const {
    *x = velocity_[0]; *y = velocity_[1]; *z = velocity_[2];
    return true;
}

bool cGZSndSys::SetVelocity(float x, float y, float z) {
    velocity_[0] = x; velocity_[1] = y; velocity_[2] = z;
    ForEachRendererInterface<cIGZSnd3DListenerEx>(0x5b30e7b3, [&](cIGZSnd3DListenerEx& l) { l.SetVelocity(x, y, z); });
    return true;
}

bool cGZSndSys::GetAll(float* px, float* py, float* pz, float* vx, float* vy, float* vz, float* fx, float* fy, float* fz,
                       float* ux, float* uy, float* uz) const {
    *px = position_[0]; *py = position_[1]; *pz = position_[2];
    *fx = orientation_[0]; *fy = orientation_[1]; *fz = orientation_[2];
    *ux = orientation_[3]; *uy = orientation_[4]; *uz = orientation_[5];
    *vx = velocity_[0]; *vy = velocity_[1]; *vz = velocity_[2];
    return true;
}

// @ 0x10048e0a0: the arguments are NOT applied (the original re-sends the stored values to the
// renderers and returns true).
bool cGZSndSys::SetAll(float, float, float, float, float, float, float, float, float, float, float, float) {
    const float p[3] = {position_[0], position_[1], position_[2]};
    const float o[6] = {orientation_[0], orientation_[1], orientation_[2], orientation_[3], orientation_[4], orientation_[5]};
    const float v[3] = {velocity_[0], velocity_[1], velocity_[2]};
    Guard guard(rendererLock_);
    std::vector<cRZAutoRefCount<cIGZSndRenderer>> snapshot(renderers_.begin(), renderers_.end());
    for (auto& renderer : snapshot) {
        void* rawListener = nullptr;
        if (!renderer->QueryInterface(0x406077d2, &rawListener) || !rawListener) continue;
        auto* listener = static_cast<cIGZSnd3DListener*>(rawListener);
        void* rawEx = nullptr;
        if (renderer->QueryInterface(0x5b30e7b3, &rawEx) && rawEx) {
            auto* ex = static_cast<cIGZSnd3DListenerEx*>(rawEx);
            ex->SetVelocity(v[0], v[1], v[2]);
            ex->Release();
        }
        listener->SetOrientation(o[0], o[1], o[2], o[3], o[4], o[5]);
        listener->SetPosition(p[0], p[1], p[2]);
        listener->Release();
    }
    return true;
}

bool cGZSndSys::GetDopplerFactor(float* factor) const { *factor = doppler_; return true; }
bool cGZSndSys::GetRolloffFactor(float* factor) const { *factor = rolloff_; return true; }
bool cGZSndSys::GetDistanceFactor(float* factor) const { *factor = distance_; return true; }

bool cGZSndSys::SetDopplerFactor(float factor) {
    doppler_ = factor;
    ForEachRendererInterface<cIGZSnd3DListenerEx>(0x5b30e7b3, [&](cIGZSnd3DListenerEx& l) { l.SetDopplerFactor(factor); });
    return true;
}

bool cGZSndSys::SetRolloffFactor(float factor) {
    rolloff_ = factor;
    ForEachRendererInterface<cIGZSnd3DListenerEx>(0x5b30e7b3, [&](cIGZSnd3DListenerEx& l) { l.SetRolloffFactor(factor); });
    return true;
}

bool cGZSndSys::SetDistanceFactor(float factor) {
    distance_ = factor;
    ForEachRendererInterface<cIGZSnd3DListenerEx>(0x5b30e7b3, [&](cIGZSnd3DListenerEx& l) { l.SetDistanceFactor(factor); });
    return true;
}

// @ 0x100499b00 / 0x10049a100: forwarded to every effects-capable renderer; the result is the
// last renderer's (true when there is none).
bool cGZSndSys::InitializeEffectsBus(std::int32_t bus, float level, std::int32_t a, std::int32_t b, std::int32_t c) {
    bool result = true;
    ForEachRendererInterface<cIGZSndEaRendererEffects>(kGZIID_cIGZSndEaRendererEffects, [&](cIGZSndEaRendererEffects& e) {
        result = e.InitializeEffectsBus(bus, level, a, b, c);
    });
    return result;
}

bool cGZSndSys::SetEffectMasterLevel(std::int32_t bus, float level) {
    bool result = true;
    ForEachRendererInterface<cIGZSndEaRendererEffects>(kGZIID_cIGZSndEaRendererEffects, [&](cIGZSndEaRendererEffects& e) {
        result = e.SetEffectMasterLevel(bus, level);
    });
    return result;
}

// ============================================================================================
// extension -> default data source class id
// ============================================================================================

// Extensions are lower-cased, trimmed and stripped of a leading '.'; empty names are ignored.
bool cGZSndSys::SetExtensionDefaultDataSourceClassId(const cIGZString& extension, std::uint32_t classId) {
    const std::string key = NormalizeExtension(extension);
    if (key.empty()) return false;
    extensionMap_[key] = classId;
    return true;
}

bool cGZSndSys::GetExtensionDefaultDataSourceClassId(const cIGZString& extension, std::uint32_t& classId) {
    const auto it = extensionMap_.find(NormalizeExtension(extension));
    if (it == extensionMap_.end()) return false;
    classId = it->second;
    return true;
}

bool cGZSndSys::GetExtensionDefaultDataSourceClassIdMappingClassId(std::uint32_t index, std::uint32_t& classId) {
    if (index >= extensionMap_.size()) return false;
    auto it = extensionMap_.begin();
    std::advance(it, index);
    classId = it->second;
    return true;
}

bool cGZSndSys::GetExtensionDefaultDataSourceClassIdMappingExtension(std::uint32_t index, cIGZString& extension) {
    if (index >= extensionMap_.size()) return false;
    auto it = extensionMap_.begin();
    std::advance(it, index);
    extension.AssignFromChars(it->first.data(), static_cast<std::uint32_t>(it->first.size()));
    return true;
}

bool cGZSndSys::GetSndPropertyCacheByIid(cIGZSndPropertyCache** out) {
    *out = propertyCache_.get();
    if (*out) (*out)->AddRef();
    return *out != nullptr;
}

bool cGZSndSys::GetSndInstanceCacheByIid(cIGZSndInstanceCache** out) {
    *out = instanceCache_.get();
    if (*out) (*out)->AddRef();
    return *out != nullptr;
}

void cGZSndSys::AddToMainlineReleaseList(cIGZUnknown* object) {
    mainlineRelease_.push_back(cRZAutoRefCount<cIGZUnknown>(object));
}
