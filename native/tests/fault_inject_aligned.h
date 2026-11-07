#pragma once
// Included once by fault harnesses that already define fail_after and
// fault_delivered. Keep aligned families intercepted even on libraries that
// route normally aligned PMR allocations through ordinary operator new.
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>
#if defined(_MSC_VER)
#include <malloc.h>
#endif
#ifndef AETHER_TEST_ALIGNED_NOTE
#define AETHER_TEST_ALIGNED_NOTE() ((void)0)
#endif
#ifndef AETHER_TEST_ALIGNED_REJECT
#define AETHER_TEST_ALIGNED_REJECT() (fail_after >= 0 && fail_after-- == 0)
#endif
#ifndef AETHER_TEST_ALIGNED_ALLOCATED
#define AETHER_TEST_ALIGNED_ALLOCATED() ((void)0)
#endif
#ifndef AETHER_TEST_ALIGNED_FREED
#define AETHER_TEST_ALIGNED_FREED(p) ((void)0)
#endif
#ifndef AETHER_TEST_ALIGNED_SYSTEM_FREE
#define AETHER_TEST_ALIGNED_SYSTEM_FREE(p) std::free(p)
#endif
static size_t aligned_test_calls = 0, aligned_test_failures = 0;
static void* fault_aligned_allocate(size_t size, std::align_val_t alignment) {
    ++aligned_test_calls;
    AETHER_TEST_ALIGNED_NOTE();
    if (AETHER_TEST_ALIGNED_REJECT()) {
        fault_delivered = true; ++aligned_test_failures; throw std::bad_alloc();
    }
    size_t align = static_cast<size_t>(alignment);
    if (align < alignof(void*)) align = alignof(void*);
    if ((align & (align - 1)) != 0) throw std::bad_alloc();
    if (!size) size = 1;
    if (size > std::numeric_limits<size_t>::max() - (align - 1)) throw std::bad_alloc();
    size = (size + align - 1) & ~(align - 1);
#if defined(_MSC_VER)
    void* result = _aligned_malloc(size, align);
#else
    void* result = std::aligned_alloc(align, size);
#endif
    if (!result) throw std::bad_alloc();
    AETHER_TEST_ALIGNED_ALLOCATED();
    return result;
}
static void fault_aligned_free(void* value) noexcept {
    AETHER_TEST_ALIGNED_FREED(value);
#if defined(_MSC_VER)
    _aligned_free(value);
#else
    AETHER_TEST_ALIGNED_SYSTEM_FREE(value);
#endif
}
void* operator new(std::size_t n, std::align_val_t a) { return fault_aligned_allocate(n, a); }
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try { return ::operator new(n, a); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try { return ::operator new[](n, a); } catch (...) { return nullptr; }
}
void operator delete(void* p, std::align_val_t) noexcept { fault_aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { fault_aligned_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { fault_aligned_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { fault_aligned_free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { fault_aligned_free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { fault_aligned_free(p); }
#undef AETHER_TEST_ALIGNED_NOTE

#undef AETHER_TEST_ALIGNED_REJECT
#undef AETHER_TEST_ALIGNED_ALLOCATED
#undef AETHER_TEST_ALIGNED_FREED
#undef AETHER_TEST_ALIGNED_SYSTEM_FREE
