#pragma once

#include <cassert>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace aether::style {

// A borrowed, non-contiguous range. It never claims that separately constructed
// objects form a T array. The caller must retain the owner for the entire use.
template<class T> class ReadOnlyArrayView {
    using Getter = const T& (*)(const void*, size_t) noexcept;
    const void* storage_ = nullptr;
    Getter get_ = nullptr;
    size_t size_ = 0;
    template<class> friend class ImmutableArray;
    ReadOnlyArrayView(const void* storage, Getter get, size_t size) noexcept
        : storage_(storage), get_(get), size_(size) {}
public:
    ReadOnlyArrayView() noexcept = default;
    size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }
    const T& operator[](size_t index) const noexcept {
        assert(index < size_ && get_); return get_(storage_, index);
    }
    const T& at(size_t index) const {
        if (index >= size_) throw std::out_of_range("immutable array index");
        return (*this)[index];
    }
    class const_iterator {
        const void* storage_ = nullptr;
        Getter get_ = nullptr;
        size_t index_ = 0, size_ = 0;
        friend class ReadOnlyArrayView;
        const_iterator(const void* storage, Getter get, size_t index, size_t size) noexcept
            : storage_(storage), get_(get), index_(index), size_(size) {}
    public:
        using iterator_category = std::forward_iterator_tag;
        using iterator_concept = std::forward_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = const T&;
        using pointer = const T*;
        const_iterator() noexcept = default;
        reference operator*() const noexcept { assert(index_ < size_ && get_); return get_(storage_, index_); }
        pointer operator->() const noexcept { return std::addressof(operator*()); }
        const_iterator& operator++() noexcept { assert(index_ < size_); ++index_; return *this; }
        const_iterator operator++(int) noexcept { auto previous = *this; ++*this; return previous; }
        friend bool operator==(const const_iterator& a, const const_iterator& b) noexcept {
            return a.storage_ == b.storage_ && a.get_ == b.get_ && a.index_ == b.index_;
        }
    };
    const_iterator begin() const noexcept { return {storage_, get_, 0, size_}; }
    const_iterator end() const noexcept { return {storage_, get_, size_, size_}; }
};

// Each slot is a real byte array which provides storage (C++20 [intro.object])
// for one actual const T object. Slot[] is a real array; indexing is performed
// on slots, NEVER on a purported T[]. alignas handles over-aligned T as well.
// Mutable slot storage remains entirely private; writing over a live const T
// would be undefined behavior. No mutable T*, span, data(), or release escapes.
template<class T> class ImmutableArray {
    static_assert(!std::is_const_v<T> && !std::is_reference_v<T>);
    static_assert(std::is_nothrow_destructible_v<T>);
    struct Slot { alignas(T) std::byte bytes[sizeof(T)]; };
    struct Storage {
        std::unique_ptr<Slot[]> slots;
        const size_t size;
        template<class Generator> Storage(size_t count, Generator&& generate)
            : slots(count ? new Slot[count] : nullptr), size(count) {
            size_t built = 0;
            try {
                for (; built < count; ++built)
                    ::new (static_cast<void*>(slots[built].bytes)) const T(generate(built));
            } catch (...) {
                while (built) std::destroy_at(element(--built));
                throw;
            }
        }
        const T* element(size_t index) const noexcept {
            return std::launder(reinterpret_cast<const T*>(slots[index].bytes));
        }
        ~Storage() noexcept {
            for (size_t i = size; i; --i) std::destroy_at(element(i - 1));
        }
    };
    std::shared_ptr<const Storage> storage_;
    explicit ImmutableArray(std::shared_ptr<const Storage> storage) noexcept : storage_(std::move(storage)) {}
    static const T& read(const void* storage, size_t index) noexcept {
        return *static_cast<const Storage*>(storage)->element(index);
    }
public:
    ImmutableArray() noexcept = default;
    size_t size() const noexcept { return storage_ ? storage_->size : 0; }
    bool empty() const noexcept { return size() == 0; }
    ReadOnlyArrayView<T> view() const noexcept {
        return storage_ ? ReadOnlyArrayView<T>{storage_.get(), &read, size()} : ReadOnlyArrayView<T>{};
    }
    template<class Generator> static ImmutableArray generate(size_t count, Generator&& generate) {
        if (!count) return {};
        if (count > std::numeric_limits<size_t>::max() / sizeof(Slot) ||
            count > static_cast<size_t>(std::numeric_limits<std::ptrdiff_t>::max()))
            throw std::length_error("immutable array size overflow");
        return ImmutableArray(std::make_shared<const Storage>(count, std::forward<Generator>(generate)));
    }
};

} // namespace aether::style
