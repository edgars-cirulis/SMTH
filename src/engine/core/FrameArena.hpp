#pragma once

#include <cstddef>
#include <memory>
#include <memory_resource>

template <size_t InlineBytes = 256 * 1024>
class FrameArena {
   public:
    FrameArena() : buffer(std::make_unique<std::byte[]>(InlineBytes)), mono(buffer.get(), InlineBytes, std::pmr::new_delete_resource()) {}

    FrameArena(const FrameArena&) = delete;
    FrameArena& operator=(const FrameArena&) = delete;
    FrameArena(FrameArena&&) = delete;
    FrameArena& operator=(FrameArena&&) = delete;

    std::pmr::memory_resource* resource() { return &mono; }

    void reset() { mono.release(); }

   private:
    std::unique_ptr<std::byte[]> buffer;
    std::pmr::monotonic_buffer_resource mono;
};
