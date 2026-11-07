#include "frozen_c_string.h"
#include <limits>
#include <memory_resource>
#include <new>
#include <stdexcept>
namespace aether::style {
struct FrozenCString::Storage {
    const size_t length;
    const char* const bytes;
    static const char* copy(std::string_view text) {
        if (text.size() >= static_cast<size_t>(PTRDIFF_MAX)) throw std::length_error("frozen C string size");
        // allocate creates a const char ARRAY object in C++20, not just slots.
        auto* p = std::pmr::polymorphic_allocator<const char>{std::pmr::new_delete_resource()}.allocate(text.size()+1);
        for (size_t i=0; i<text.size(); ++i)
            ::new (const_cast<char*>(p+i)) const char(text[i]);
        ::new (const_cast<char*>(p+text.size())) const char('\0');
        return p;
    }
    explicit Storage(std::string_view text) : length(text.size()), bytes(copy(text)) {}
    ~Storage() noexcept {
        for (size_t i=0; i<=length; ++i) std::destroy_at(bytes+i);
        // Private raw conversion only after all element lifetimes have ended.
        std::pmr::new_delete_resource()->deallocate(const_cast<char*>(bytes), length+1, alignof(char));
    }
};
FrozenCString::FrozenCString(std::string_view text) : storage_(std::make_shared<const Storage>(text)) {}
FrozenCString::~FrozenCString() = default;
const char* FrozenCString::c_str() const noexcept { return storage_ ? storage_->bytes : nullptr; }
size_t FrozenCString::size() const noexcept { return storage_ ? storage_->length : 0; }
std::string_view FrozenCString::view() const noexcept { return storage_ ? std::string_view(storage_->bytes,storage_->length) : std::string_view{}; }
}
