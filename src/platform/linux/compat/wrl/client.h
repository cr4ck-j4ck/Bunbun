// Bunbun — Microsoft::WRL::ComPtr for the Linux build (see ../windows.h).
// Works with the reference-counted Direct2D stand-ins in ../d2d1.h.
#pragma once

#include <utility>

namespace Microsoft::WRL {

template <class T>
class ComPtr;

// What &ptr gives, as in WRL: an out-parameter T** (releasing what it held)
// or the ComPtr itself.
template <class T>
class ComPtrRef {
public:
    explicit ComPtrRef(ComPtr<T>* p) : p_(p) {}
    operator T**() { return p_->ReleaseAndGetAddressOf(); }
    operator void**() { return reinterpret_cast<void**>(p_->ReleaseAndGetAddressOf()); }
    operator ComPtr<T>*() { return p_; }
    T** operator&() = delete;

private:
    ComPtr<T>* p_;
};

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    ComPtr(T* p) : p_(p) { if (p_) p_->AddRef(); }
    ComPtr(const ComPtr& o) : p_(o.p_) { if (p_) p_->AddRef(); }
    ComPtr(ComPtr&& o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    template <class U>
    ComPtr(const ComPtr<U>& o) : p_(o.Get()) { if (p_) p_->AddRef(); }
    ~ComPtr() { Reset(); }

    ComPtr& operator=(const ComPtr& o) { ComPtr(o).Swap(*this); return *this; }
    ComPtr& operator=(ComPtr&& o) noexcept { ComPtr(std::move(o)).Swap(*this); return *this; }
    ComPtr& operator=(T* p) { ComPtr(p).Swap(*this); return *this; }
    ComPtr& operator=(std::nullptr_t) { Reset(); return *this; }

    T* Get() const { return p_; }
    T* operator->() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
    T** GetAddressOf() { return &p_; }
    T** ReleaseAndGetAddressOf() { Reset(); return &p_; }
    ComPtrRef<T> operator&() { return ComPtrRef<T>(this); }
    void Attach(T* p) { Reset(); p_ = p; }
    T* Detach() { T* p = p_; p_ = nullptr; return p; }
    unsigned long Reset() {
        if (T* p = std::exchange(p_, nullptr)) return p->Release();
        return 0;
    }
    void Swap(ComPtr& o) { std::swap(p_, o.p_); }
    template <class U>
    long As(ComPtr<U>* out) const { return p_ ? p_->QueryInterface(IID_PPV_ARGS(out->ReleaseAndGetAddressOf())) : -1; }

private:
    T* p_ = nullptr;
};

template <class T, class U>
bool operator==(const ComPtr<T>& a, const ComPtr<U>& b) { return a.Get() == b.Get(); }

}  // namespace Microsoft::WRL
