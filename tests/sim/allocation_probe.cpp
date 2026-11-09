// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Replaces the global allocation functions of aoahid_tests so a test can count the
 * allocations a stretch of code makes (allocation_probe::begin and finish). */
#include "sim/allocation_probe.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <limits>
#include <new>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace allocation_probe {

std::atomic<bool> active{false};
std::atomic<std::size_t> count{0U};

void record() noexcept {
    if (active.load(std::memory_order_relaxed)) {
        count.fetch_add(1U, std::memory_order_relaxed);
    }
}

void* allocate(const std::size_t requested) {
    record();
    if (void* storage = std::malloc(requested == 0U ? 1U : requested); storage != nullptr) {
        return storage;
    }
    throw std::bad_alloc();
}

void* allocate_aligned(const std::size_t requested, const std::size_t alignment) {
    record();
#if defined(_MSC_VER)
    if (void* storage = _aligned_malloc(requested == 0U ? 1U : requested, alignment);
        storage != nullptr) {
        return storage;
    }
#else
    if (requested > std::numeric_limits<std::size_t>::max() - (alignment - 1U)) {
        throw std::bad_alloc();
    }
    const std::size_t adjusted =
        std::max(alignment, ((requested + alignment - 1U) / alignment) * alignment);
    if (void* storage = std::aligned_alloc(alignment, adjusted); storage != nullptr) {
        return storage;
    }
#endif
    throw std::bad_alloc();
}

void deallocate_aligned(void* storage) noexcept {
#if defined(_MSC_VER)
    _aligned_free(storage);
#else
    std::free(storage);
#endif
}

void begin() noexcept {
    count.store(0U, std::memory_order_relaxed);
    active.store(true, std::memory_order_release);
}

std::size_t finish() noexcept {
    active.store(false, std::memory_order_release);
    return count.load(std::memory_order_relaxed);
}

} // namespace allocation_probe

void* operator new(const std::size_t size) { return allocation_probe::allocate(size); }
void* operator new[](const std::size_t size) { return allocation_probe::allocate(size); }
void* operator new(const std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocate(size);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocate(size);
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* storage) noexcept { std::free(storage); }
void operator delete[](void* storage) noexcept { std::free(storage); }
void operator delete(void* storage, std::size_t) noexcept { std::free(storage); }
void operator delete[](void* storage, std::size_t) noexcept { std::free(storage); }
void operator delete(void* storage, const std::nothrow_t&) noexcept { std::free(storage); }
void operator delete[](void* storage, const std::nothrow_t&) noexcept { std::free(storage); }

#if defined(__cpp_aligned_new)
void* operator new(const std::size_t size, const std::align_val_t alignment) {
    return allocation_probe::allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void* operator new[](const std::size_t size, const std::align_val_t alignment) {
    return allocation_probe::allocate_aligned(size, static_cast<std::size_t>(alignment));
}
void* operator new(const std::size_t size, const std::align_val_t alignment,
                   const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocate_aligned(size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](const std::size_t size, const std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
    try {
        return allocation_probe::allocate_aligned(size, static_cast<std::size_t>(alignment));
    } catch (...) {
        return nullptr;
    }
}
void operator delete(void* storage, std::align_val_t) noexcept {
    allocation_probe::deallocate_aligned(storage);
}
void operator delete[](void* storage, std::align_val_t) noexcept {
    allocation_probe::deallocate_aligned(storage);
}
void operator delete(void* storage, std::size_t, std::align_val_t) noexcept {
    allocation_probe::deallocate_aligned(storage);
}
void operator delete[](void* storage, std::size_t, std::align_val_t) noexcept {
    allocation_probe::deallocate_aligned(storage);
}
void operator delete(void* storage, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_probe::deallocate_aligned(storage);
}
void operator delete[](void* storage, std::align_val_t, const std::nothrow_t&) noexcept {
    allocation_probe::deallocate_aligned(storage);
}
#endif
