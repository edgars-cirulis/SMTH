#pragma once

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

template <typename Signature, size_t InlineBytes = 64>
class SmallFn;

template <typename R, typename... Args, size_t InlineBytes>
class SmallFn<R(Args...), InlineBytes> {
   public:
    SmallFn() = default;
    SmallFn(std::nullptr_t) {}

    SmallFn(const SmallFn&) = delete;
    SmallFn& operator=(const SmallFn&) = delete;

    SmallFn(SmallFn&& other) noexcept { moveFrom(std::move(other)); }
    SmallFn& operator=(SmallFn&& other) noexcept
    {
        if (this != &other) {
            reset();
            moveFrom(std::move(other));
        }
        return *this;
    }

    ~SmallFn() { reset(); }

    template <typename F>
    SmallFn(F&& f)
    {
        emplace<std::decay_t<F>>(std::forward<F>(f));
    }

    template <typename F>
    SmallFn& operator=(F&& f)
    {
        reset();
        emplace<std::decay_t<F>>(std::forward<F>(f));
        return *this;
    }

    explicit operator bool() const { return invoke_ != nullptr; }

    R operator()(Args... args) const { return invoke_(storagePtr(), std::forward<Args>(args)...); }

    void reset()
    {
        if (destroy_) {
            destroy_(storagePtr());
        }
        invoke_ = nullptr;
        destroy_ = nullptr;
        move_ = nullptr;
    }

   private:
    using InvokeFn = R (*)(void*, Args&&...);
    using DestroyFn = void (*)(void*);
    using MoveFn = void (*)(void*, void*);

    alignas(std::max_align_t) unsigned char storage_[InlineBytes]{};
    InvokeFn invoke_ = nullptr;
    DestroyFn destroy_ = nullptr;
    MoveFn move_ = nullptr;

    void* storagePtr() const { return const_cast<unsigned char*>(storage_); }

    template <typename F>
    void emplace(F&& f)
    {
        using T = std::decay_t<F>;
        static_assert(sizeof(T) <= InlineBytes, "Callable too large for SmallFn inline storage");
        static_assert(std::is_move_constructible_v<T>, "Callable must be move constructible");

        new (storage_) T(std::forward<F>(f));

        invoke_ = [](void* p, Args&&... a) -> R { return (*reinterpret_cast<T*>(p))(std::forward<Args>(a)...); };
        destroy_ = [](void* p) { reinterpret_cast<T*>(p)->~T(); };
        move_ = [](void* dst, void* src) {
            new (dst) T(std::move(*reinterpret_cast<T*>(src)));
            reinterpret_cast<T*>(src)->~T();
        };
    }

    void moveFrom(SmallFn&& other) noexcept
    {
        invoke_ = other.invoke_;
        destroy_ = other.destroy_;
        move_ = other.move_;
        if (other.invoke_) {
            other.move_(storagePtr(), other.storagePtr());
            other.invoke_ = nullptr;
            other.destroy_ = nullptr;
            other.move_ = nullptr;
        }
    }
};
