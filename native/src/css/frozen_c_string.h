#pragma once
#include <memory>
#include <string_view>
namespace aether::style {
// Contiguous actual const-char array. Borrowed readers require this value's pin.
// A default value is absent; a present empty value owns its trailing NUL.
class FrozenCString {
    struct Storage;
    const std::shared_ptr<const Storage> storage_;
public:
    FrozenCString() noexcept = default;
    explicit FrozenCString(std::string_view);
    FrozenCString(const FrozenCString&) noexcept = default;
    FrozenCString(FrozenCString&& other) noexcept : storage_(other.storage_) {}
    ~FrozenCString();
    bool present() const noexcept { return bool(storage_); }
    const char* c_str() const noexcept;
    size_t size() const noexcept;
    std::string_view view() const noexcept;
};
}
