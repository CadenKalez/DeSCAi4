#include "rz/cRZCOMDllDirector.h"

static RZHeapSizeFn g_heapSize = nullptr;
void SetRZHeapSizeFn(RZHeapSizeFn fn) { g_heapSize = fn; }

bool cRZCOMDllDirector::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZFrameWorkHooks) {
        *out = static_cast<cIGZFrameWorkHooks*>(this);
    } else if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIRZCOMDllDirector) {
        *out = static_cast<cIGZCOMDirector*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cRZCOMDllDirector::RemoveRef() {
    if (refCount_ != 0) --refCount_;
    return refCount_;
}

bool cRZCOMDllDirector::InitializeCOM(cIGZCOM* com, const cIGZString& libraryPath) {
    if (!com) return false;
    com_ = com;
    framework_ = com->GetFrameWork();
    libraryPath_.AssignFromIGZString(libraryPath);
    for (cRZCOMDllDirector* child : children_) child->InitializeCOM(com, libraryPath);
    return true;
}

bool cRZCOMDllDirector::GetClassObject(std::uint32_t clsid, std::uint32_t iid, void** out) {
    for (cRZCOMDllDirector* child : children_) {
        if (child->GetClassObject(clsid, iid, out)) return true;
    }
    const auto it = classes_.find(clsid);
    if (it == classes_.end()) return false;
    if (it->second.kind == 2) return reinterpret_cast<MakeFn>(it->second.fn)(iid, out);
    if (it->second.kind == 1) {
        cIGZUnknown* object = reinterpret_cast<CreateFn>(it->second.fn)();
        if (!object) return false;     // the class is not linked into this build
        if (object->QueryInterface(iid, out)) return true;
        object->AddRef();     // the object was never referenced: take and drop one to destroy it
        object->Release();
    }
    return false;
}

void cRZCOMDllDirector::EnumClassObjects(void (*fn)(std::uint32_t, std::uint32_t, void*), void* context) {
    for (cRZCOMDllDirector* child : children_) child->EnumClassObjects(fn, context);
    for (const auto& entry : classes_) fn(entry.first, 0, context);
}

bool cRZCOMDllDirector::GetLibraryPath(cIGZString& out) {
    out.AssignFromChars(libraryPath_.Data(), libraryPath_.Strlen());
    return true;
}

// Adopts a child: initialises it with our COM and path, adopts its own children first, then keeps it.
void cRZCOMDllDirector::AddDirector(cRZCOMDllDirector* child) {
    child->InitializeCOM(GZCOM(), libraryPath_);
    for (cRZCOMDllDirector* grandchild : child->children_) AddDirector(grandchild);
    children_.push_back(child);
}

bool cRZCOMDllDirector::CanUnloadNow() {
    if (refCount_ == 0) {
        for (cRZCOMDllDirector* child : children_) {
            if (!child->CanUnloadNow()) return false;
        }
    }
    return true;
}

std::size_t cRZCOMDllDirector::GetHeapAllocatedSize() { return g_heapSize ? g_heapSize() : 0; }

void cRZCOMDllDirector::AddCls(std::uint32_t clsid, CreateFn create) {
    classes_.emplace(clsid, Entry{reinterpret_cast<void*>(create), 1});     // first registration wins
}

void cRZCOMDllDirector::AddCls(std::uint32_t clsid, MakeFn make) {
    classes_.emplace(clsid, Entry{reinterpret_cast<void*>(make), 2});
}

static IDirectorFramework* g_framework = nullptr;
IDirectorFramework* DirectorFramework() { return g_framework; }
void SetDirectorFramework(IDirectorFramework* framework) { g_framework = framework; }

bool cRZCOMDllDirector::StartServices(cIGZCOM* com, std::initializer_list<std::uint32_t> serviceClasses) {
    IDirectorFramework* framework = DirectorFramework();
    framework->AddHooks(static_cast<cIGZFrameWorkHooks*>(this));
    for (const std::uint32_t clsid : serviceClasses) {
        cIGZUnknown* service = nullptr;
        com->CreateInstance(clsid, kGZIID_cIGZSystemService, reinterpret_cast<void**>(&service));
        if (service) {
            framework->AddSystemService(service);
            service->Release();
        }
    }
    return true;
}
