#pragma once
#include "frozen_node_record.h"
#include <array>
#include <iterator>
#include <limits>
#include <span>
#include <unordered_map>
namespace aether::style {
using FrozenNodeIndex = std::unordered_map<const aether_node_s*, FrozenNodeRef>;
struct BackingBuildInput { FrozenNodeIndex entries; };
struct BackingPatchEntry {
    const aether_node_s* node = nullptr;
    // A null record removes the key. Repeated keys are applied in input order.
    FrozenNodeRef record;
};
class ImmutableStyleBacking;
using BackingRef = std::shared_ptr<const ImmutableStyleBacking>;
aether_status_t freeze_backing(const BackingBuildInput&, BackingRef&) noexcept;
aether_status_t patch_backing(const BackingRef&, std::span<const BackingPatchEntry>, BackingRef&) noexcept;
class ImmutableStyleBacking {
    // Compressed binary radix tree. Branch bits strictly decrease on a path, so
    // lookup, path copying and iterator storage are bounded by pointer width,
    // independently of revision count. No node retains a previous backing.
    struct Node;
    using NodeRef = std::shared_ptr<const Node>;
    using Entry = std::pair<const aether_node_s*, FrozenNodeRef>;
    static constexpr size_t key_bits = std::numeric_limits<uintptr_t>::digits;
    struct Node {
        const NodeRef zero, one;
        const Entry entry;
        const unsigned bit;
        explicit Node(Entry e) : entry(std::move(e)), bit(0) {}
        Node(unsigned b, NodeRef z, NodeRef o)
            : zero(std::move(z)), one(std::move(o)), entry(), bit(b) {}
        bool leaf() const noexcept { return bool(entry.first); }
    };
    const NodeRef root_;
    const size_t size_;
    explicit ImmutableStyleBacking(NodeRef root, size_t size) : root_(std::move(root)), size_(size) {}
    static bool one(uintptr_t key, unsigned bit) noexcept { return ((key >> bit) & 1) != 0; }
    static const Node* find_leaf(const Node* node, uintptr_t key) noexcept;
    static NodeRef build_sorted(std::span<const Entry>);
    static NodeRef replace(const NodeRef&, uintptr_t, const FrozenNodeRef&);
    static NodeRef insert_at(const NodeRef&, uintptr_t, unsigned, const NodeRef&);
    static NodeRef upsert(const NodeRef&, const Entry&, bool&);
    static NodeRef erase(const NodeRef&, uintptr_t, bool&);
    friend aether_status_t freeze_backing(const BackingBuildInput&, BackingRef&) noexcept;
    friend aether_status_t patch_backing(const BackingRef&, std::span<const BackingPatchEntry>, BackingRef&) noexcept;
public:
    class ConstIterator {
        std::array<const Node*, key_bits> pending_{};
        size_t depth_ = 0;
        const Node* current_ = nullptr;
        void descend(const Node* node) noexcept {
            while (node && !node->leaf()) {
                pending_[depth_++] = node->one.get();
                node = node->zero.get();
            }
            current_ = node;
        }
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Entry;
        using difference_type = std::ptrdiff_t;
        using pointer = const Entry*;
        using reference = const Entry&;
        ConstIterator() noexcept = default;
        explicit ConstIterator(const Node* root) noexcept { descend(root); }
        reference operator*() const noexcept { return current_->entry; }
        pointer operator->() const noexcept { return &current_->entry; }
        ConstIterator& operator++() noexcept {
            descend(depth_ ? pending_[--depth_] : nullptr); return *this;
        }
        ConstIterator operator++(int) noexcept { auto previous=*this; ++*this; return previous; }
        friend bool operator==(const ConstIterator& a, const ConstIterator& b) noexcept { return a.current_==b.current_; }
    };
    class EntriesView {
        const Node* root_;
        size_t size_;
    public:
        EntriesView(const Node* root, size_t size) noexcept : root_(root), size_(size) {}
        ConstIterator begin() const noexcept { return ConstIterator(root_); }
        ConstIterator end() const noexcept { return {}; }
        size_t size() const noexcept { return size_; }
        bool empty() const noexcept { return !size_; }
    };
    size_t size() const noexcept { return size_; }
    const FrozenNodeRecord* find(const aether_node_s* node) const noexcept {
        const auto* leaf=find_leaf(root_.get(),reinterpret_cast<uintptr_t>(node));
        return leaf && leaf->entry.first==node ? leaf->entry.second.get() : nullptr;
    }
    FrozenNodeRef find_owner(const aether_node_s* node) const noexcept {
        const auto* leaf=find_leaf(root_.get(),reinterpret_cast<uintptr_t>(node));
        return leaf && leaf->entry.first==node ? leaf->entry.second : FrozenNodeRef{};
    }
    // Unspecified directory order, never the public revision manifest. Borrowed
    // view and iterators require this backing to remain alive; reads allocate 0.
    EntriesView entries() const noexcept { return {root_.get(),size_}; }
};
}
