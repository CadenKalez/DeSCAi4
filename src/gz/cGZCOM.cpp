#include "gz/cGZCOM.h"

#include <chrono>
#include <ctime>
#include <thread>

#include "rz/cRZDate.h"

int GetGuidGenSeconds() {
    const cRZDate epoch(2, 2, 1997);
    const cRZDate today;
    std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    const cRZDate localDate(static_cast<std::uint32_t>(local->tm_mon + 1), static_cast<std::uint32_t>(local->tm_mday),
                            static_cast<std::uint32_t>(local->tm_year + 1900));
    std::uint32_t seconds = 0;
    if (localDate.JulianDay() != 0) seconds = static_cast<std::uint32_t>(local->tm_min * 60 + local->tm_sec + local->tm_hour * 3600);
    return static_cast<int>(seconds % 60 + ((seconds % 3600) / 60) * 60 + ((seconds % 86400) / 3600) * 3600 +
                            (today.JulianDay() - epoch.JulianDay()) * 86400);
}

bool cGZCOM::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZCOMExtended) {
        *out = static_cast<cIGZCOMLibraryManager*>(this);
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

bool cGZCOM::RealInit() {
    if (initialised_) return true;
    framework_ = host_->Framework();
    director_ = host_->ProcessDirector();
    cRZString path;
    host_->CurrentAppPath(path);
    if (!director_->InitializeCOM(this, path)) return false;
    initialised_ = true;
    return true;
}

void cGZCOM::RealShutdown() {
    if (!initialised_) return;
    FreeAllLibraries();
    framework_ = nullptr;
    initialised_ = false;
}

void cGZCOM::FreeAllLibraries() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto& entry : libraries_)
        if (entry.second->IsLoaded()) entry.second->MarkUnloaded();
}

void cGZCOM::FreeUnusedLibraries() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto& entry : libraries_) {
        cGZCOMLibrary& library = *entry.second;
        if (!library.IsLoaded()) continue;
        cIGZCOMDirector* director = library.GetDirector();
        if (director && !director->CanUnloadNow()) continue;
        library.MarkUnloaded();
    }
}

bool cGZCOM::UpdateClassRegistry(cGZCOMLibrary& library) {
    if (!library.Load()) return false;
    cIGZCOMDirector* director = library.GetDirector();
    std::lock_guard<std::recursive_mutex> guard(lock_);
    registering_ = &library;
    director->EnumClassObjects(AddEntryCallback, this);
    return true;
}

// A class registered again by a newer version replaces the older entry; the library being registered owns it.
void cGZCOM::AddEntryCallback(std::uint32_t clsid, std::uint32_t version, void* context) {
    auto* self = static_cast<cGZCOM*>(context);
    auto found = self->classes_.find(clsid);
    if (found != self->classes_.end()) {
        if (found->second.version >= version) return;
        self->classes_.erase(found);
    }
    self->classes_[clsid] = {version, self->registering_};
}

bool cGZCOM::AddLibrary(const cIGZString& path) {
    cGZCOMLibrary probe(path);
    std::unique_lock<std::recursive_mutex> lock(lock_);
    const std::string key(path.Data(), path.Strlen());
    auto found = libraries_.find(key);
    if (found != libraries_.end()) return true;
    auto& slot = libraries_[key];
    slot = std::make_unique<cGZCOMLibrary>(path);
    cGZCOMLibrary* library = slot.get();
    lock.unlock();
    if (!UpdateClassRegistry(*library)) return false;
    if (cIGZCOMDirector* director = library->GetDirector()) director->OnStart(this);
    return true;
}

cGZCOMLibrary* cGZCOM::AddLoadedLibrary(const cIGZString& path, cIGZCOMDirector* director) {
    std::unique_lock<std::recursive_mutex> lock(lock_);
    auto& slot = libraries_[std::string(path.Data(), path.Strlen())];
    slot = std::make_unique<cGZCOMLibrary>(path);
    slot->MarkLoaded(director, director);
    cGZCOMLibrary* library = slot.get();
    registering_ = library;
    director->EnumClassObjects(AddEntryCallback, this);
    return library;
}

bool cGZCOM::GetLibObject(cGZCOMLibrary& library, std::uint32_t clsid, std::uint32_t iid, void** out) {
    if (!library.IsLoaded() && !library.Load()) return false;
    return library.GetDirector()->GetClassObject(clsid, iid, out);
}

bool cGZCOM::GetClassObject(std::uint32_t clsid, std::uint32_t iid, void** out) {
    std::unique_lock<std::recursive_mutex> lock(lock_);
    auto found = classes_.find(clsid);
    if (found != classes_.end()) {
        cGZCOMLibrary* library = found->second.library;
        lock.unlock();
        return GetLibObject(*library, clsid, iid, out);
    }
    lock.unlock();
    if (clsid != kGZCOMFrameworkClass) return director_->GetClassObject(clsid, iid, out);
    return framework_->QueryInterface(iid, out);
}

std::uint32_t cGZCOM::EnumerateLibraries(cGZCOMLibrary** out, std::uint32_t& count) {
    const std::uint32_t total = static_cast<std::uint32_t>(libraries_.size());
    if (!out) {
        count = total;
        return total;
    }
    if (total < count) count = total;
    std::uint32_t written = 0;
    for (auto& entry : libraries_) {
        if (written >= count) break;
        out[written++] = entry.second.get();
        entry.second->AddRef();
    }
    return count;
}

void cGZCOM::EnumerateLibraries(bool (*callback)(cGZCOMLibrary*, void*), void* context) {
    if (!callback) return;
    for (auto& entry : libraries_)
        if (!callback(entry.second.get(), context)) break;
}

bool cGZCOM::CreateGuid(std::uint32_t* out) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    while (nextGuidSecond_ > static_cast<std::int32_t>(std::time(nullptr))) std::this_thread::sleep_for(std::chrono::microseconds(250000));
    const std::uint32_t counter = random_.RandomUint32Uniform(8);
    *out = (static_cast<std::uint32_t>(GetGuidGenSeconds()) & 0x1fffffffu) | (counter << 29);
    nextGuidSecond_ = static_cast<std::int32_t>(std::time(nullptr)) + 1;
    return true;
}

bool cGZCOM::CreateGuids(std::uint32_t* out, std::uint32_t count) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (count != 0) {
        while (nextGuidSecond_ > static_cast<std::int32_t>(std::time(nullptr))) std::this_thread::sleep_for(std::chrono::microseconds(250000));
        const std::uint32_t base = static_cast<std::uint32_t>(GetGuidGenSeconds());
        std::uint32_t top = 0;
        for (std::uint32_t i = 0; i < count; ++i, top += 0x20000000u) out[i] = ((((i >> 3) & 0x1fffffffu) + base) & 0x1fffffffu) | top;
        nextGuidSecond_ = static_cast<std::int32_t>(std::time(nullptr)) + static_cast<std::int32_t>((count + 7) >> 3);
    }
    return true;
}
