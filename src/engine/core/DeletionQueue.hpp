#pragma once

#include <cstddef>
#include <utility>
#include <vector>

#include "engine/core/SmallFn.hpp"

class DeletionQueue {
   public:
    void reserve(size_t n) { fns.reserve(n); }
    void push(SmallFn<void()>&& fn) { fns.push_back(std::move(fn)); }

    template <typename F>
    void push(F&& fn)
    {
        fns.emplace_back(std::forward<F>(fn));
    }

    void flush()
    {
        for (auto it = fns.rbegin(); it != fns.rend(); ++it)
            (*it)();
        fns.clear();
    }

    bool empty() const { return fns.empty(); }

   private:
    std::vector<SmallFn<void()>> fns;
};
