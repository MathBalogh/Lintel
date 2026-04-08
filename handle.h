#pragma once

namespace lintel {

// ---------------------------------------------------------------------------
// Owner<T>
// ---------------------------------------------------------------------------
//
// Owning, move-only wrapper around a heap-allocated implementation pointer.
// Gives the public API a stable, typed handle while keeping the internal type
// out of headers that don't need it.
//
// T is the internal type; it must be fully defined in any translation unit
// that calls impl_allocate<>().
//
template<typename T>
class Owner {
    void release() noexcept { delete iptr_; iptr_ = nullptr; }
protected:
    T* iptr_;
    
    // Allocate a fresh implementation object, optionally of a subtype.
    template<typename I = T>
    void allocate() {
        release();
        iptr_ = new I();
    }
public:
    Owner() noexcept: iptr_(nullptr) {}
    explicit Owner(T* i) noexcept: iptr_(i) {}
    ~Owner() { release(); }

    Owner(const Owner&)            = delete;
    Owner& operator=(const Owner&) = delete;

    Owner(Owner&& other) noexcept: iptr_(other.iptr_) { other.iptr_ = nullptr; }
    Owner& operator=(Owner&& other) noexcept {
        if (this != &other) {
            release();
            iptr_ = other.iptr_;
            other.iptr_ = nullptr;
        }
        return *this;
    }

    // Typed pointer access (optionally downcast to a derived impl type).
    template<typename I = T>       I* handle() { return static_cast<I*>(iptr_); }
    template<typename I = T> const I* handle() const { return static_cast<const I*>(iptr_); }

    // Reinterpret this container as another container type that wraps the same
    // raw pointer (e.g. Impl<INode> <-> WeakImpl<Node>).  Both types must have
    // an identical single-pointer memory layout.
    template<typename U> U& as() { return *reinterpret_cast<U*>(this); }

    T* operator->() { return iptr_; }
    const T* operator->() const { return iptr_; }

    explicit operator bool() const noexcept { return iptr_ != nullptr; }

    template<typename I> friend class View;
};

// ---------------------------------------------------------------------------
// View<Interface>
// ---------------------------------------------------------------------------
//
// Non-owning, copyable handle to an implementation pointer.  The stored
// pointer is void so the concrete type can vary without a new specialisation
// at the call site.
//
// Interface is the type exposed by operator-> and the default for as<>().
//
template<typename Interface>
class View {
    void* iptr_;
public:
    View() noexcept: iptr_(nullptr) {}
    explicit View(void* ptr) noexcept: iptr_(ptr) {}

    View(const View&)            = default;
    View& operator=(const View&) = default;
    View(View&&)                 = default;
    View& operator=(View&&)      = default;

    // Borrow a reference from an owning handle.
    template<typename T>
    explicit View(const Owner<T>& owner) noexcept: iptr_(owner.iptr_) {}

    // Re-point to a different owning handle without transferring ownership.
    template<typename T>
    View& operator=(const Owner<T>& owner) noexcept {
        iptr_ = owner.iptr_;
        return *this;
    }

    // Null the reference without deleting the pointed-to object.
    void reset() noexcept { iptr_ = nullptr; }

    // Typed or raw pointer access.
    template<typename I = void>       I* handle()       { return reinterpret_cast<I*>(iptr_); }
    template<typename I = void> const I* handle() const { return reinterpret_cast<const I*>(iptr_); }

    // Reinterpret this non-owning container as another container type.
    template<typename U = Interface> U& as() { return *reinterpret_cast<U*>(this); }

    Interface*       operator->()       { return reinterpret_cast<Interface*>(this); }
    const Interface* operator->() const { return reinterpret_cast<const Interface*>(this); }

    // Raw-pointer and same-type equality.
    bool operator==(const void* o) const noexcept { return iptr_ == o; }
    bool operator==(const View& o) const noexcept { return iptr_ == o.iptr_; }
    bool operator!=(const void* o) const noexcept { return iptr_ != o; }
    bool operator!=(const View& o) const noexcept { return iptr_ != o.iptr_; }

    explicit operator bool() const noexcept { return iptr_ != nullptr; }
    operator View<void>& () {
        return *reinterpret_cast<View<void>*>(this);
    }
};

} // namespace lintel
