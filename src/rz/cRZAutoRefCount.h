// cRZAutoRefCount<T> — intrusive smart pointer over cIGZUnknown-style AddRef/Release. Assignment
// AddRefs the new object before releasing the old one (the order used by every inlined copy in the
// binary, e.g. cFreshDimensionPlayer::LoadScore), so self-assignment and aliasing are safe.
#pragma once
#include <utility>

template <class T>
class cRZAutoRefCount {
public:
    cRZAutoRefCount() noexcept = default;
    explicit cRZAutoRefCount(T* p) noexcept : ptr_(p) { if (ptr_) ptr_->AddRef(); }
    cRZAutoRefCount(const cRZAutoRefCount& o) noexcept : ptr_(o.ptr_) { if (ptr_) ptr_->AddRef(); }
    cRZAutoRefCount(cRZAutoRefCount&& o) noexcept : ptr_(std::exchange(o.ptr_, nullptr)) {}
    ~cRZAutoRefCount() { if (ptr_) ptr_->Release(); }

    cRZAutoRefCount& operator=(const cRZAutoRefCount& o) noexcept { return *this = o.ptr_; }
    cRZAutoRefCount& operator=(cRZAutoRefCount&& o) noexcept {
        if (this != &o) {
            T* old = std::exchange(ptr_, std::exchange(o.ptr_, nullptr));
            if (old) old->Release();
        }
        return *this;
    }
    cRZAutoRefCount& operator=(T* p) noexcept {
        if (p != ptr_) {
            if (p) p->AddRef();
            T* old = std::exchange(ptr_, p);
            if (old) old->Release();
        }
        return *this;
    }

    T* get() const noexcept { return ptr_; }
    operator T*() const noexcept { return ptr_; }     // cRZAutoRefCount<T>::operator T*() const (all instantiations)
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    // Releases what is held and hands back the (now empty) slot for a creation call's out-parameter.
    // cRZAutoRefCount<T>::AsPPVoidParam() in every instantiation.
    cRZAutoRefCount* AsPPVoidParam() noexcept {
        if (ptr_) std::exchange(ptr_, nullptr)->Release();
        return this;
    }
    void** AsOutParam() noexcept { AsPPVoidParam(); return reinterpret_cast<void**>(&ptr_); }

private:
    T* ptr_ = nullptr;
};
