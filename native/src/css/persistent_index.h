#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aether::style {
// Immutable Patricia index. Keys include zero and UINTPTR_MAX; branch/leaf
// identity is structural, never encoded by a reserved key. Values are copied
// only into new const leaves. Nodes retain children, never a prior index/root.
// Copies of this small owner share the tree; edits copy at most one bounded path.
template<class T> class PersistentIndex {
public:
    using Entry = std::pair<uintptr_t, T>;
private:
    static constexpr size_t key_bits = std::numeric_limits<uintptr_t>::digits;
    struct Node;
    using NodeRef = std::shared_ptr<const Node>;
    struct Node {
        const NodeRef zero, one;
        const std::optional<Entry> value;
        const unsigned bit;
        explicit Node(const Entry& entry) : value(entry), bit(0) {}
        Node(unsigned selected, NodeRef left, NodeRef right)
            : zero(std::move(left)), one(std::move(right)), bit(selected) {
            assert(zero && one);
        }
        bool leaf() const noexcept { return value.has_value(); }
    };
    NodeRef root_;
    size_t size_ = 0;
    PersistentIndex(NodeRef root, size_t size) noexcept : root_(std::move(root)), size_(size) {}
    static bool one(uintptr_t key, unsigned bit) noexcept { return ((key >> bit) & 1) != 0; }
    static const Node* leaf_for(const Node* node, uintptr_t key) noexcept {
        while (node && !node->leaf()) node = (one(key,node->bit) ? node->one : node->zero).get();
        return node;
    }
    static NodeRef build_sorted(std::span<const Entry* const> entries) {
        if (entries.empty()) return {};
        if (entries.size()==1) return std::make_shared<const Node>(*entries.front());
        const uintptr_t difference = entries.front()->first ^ entries.back()->first;
        assert(difference);
        const unsigned bit = std::bit_width(difference)-1;
        const auto split = std::partition_point(entries.begin(),entries.end(),[&](const Entry* entry) {
            return !one(entry->first,bit);
        });
        const size_t count = static_cast<size_t>(split-entries.begin());
        return std::make_shared<const Node>(bit,build_sorted(entries.first(count)),build_sorted(entries.subspan(count)));
    }
    static NodeRef replace(const NodeRef& node, uintptr_t key, const T& value) {
        if (node->leaf()) return std::make_shared<const Node>(Entry{key,value});
        if (one(key,node->bit)) return std::make_shared<const Node>(node->bit,node->zero,replace(node->one,key,value));
        return std::make_shared<const Node>(node->bit,replace(node->zero,key,value),node->one);
    }
    static NodeRef insert_at(const NodeRef& node, uintptr_t key, unsigned bit, const NodeRef& leaf) {
        if (node->leaf() || node->bit<bit)
            return one(key,bit) ? std::make_shared<const Node>(bit,node,leaf) : std::make_shared<const Node>(bit,leaf,node);
        if (one(key,node->bit)) return std::make_shared<const Node>(node->bit,node->zero,insert_at(node->one,key,bit,leaf));
        return std::make_shared<const Node>(node->bit,insert_at(node->zero,key,bit,leaf),node->one);
    }
    static NodeRef erase_node(const NodeRef& node, uintptr_t key, bool& removed) {
        if (!node) return {};
        if (node->leaf()) {
            if (node->value->first!=key) return node;
            removed=true; return {};
        }
        if (one(key,node->bit)) {
            auto next=erase_node(node->one,key,removed);
            if (!removed) return node;
            if (!next) return node->zero;
            return std::make_shared<const Node>(node->bit,node->zero,std::move(next));
        }
        auto next=erase_node(node->zero,key,removed);
        if (!removed) return node;
        if (!next) return node->one;
        return std::make_shared<const Node>(node->bit,std::move(next),node->one);
    }
public:
    PersistentIndex() noexcept = default;
    // Import is intentionally complete. Sort pointers rather than values so T
    // need only be copy-constructible, not default-constructible or assignable.
    static PersistentIndex build(std::vector<Entry> entries) {
        std::vector<const Entry*> ordered;
        ordered.reserve(entries.size());
        for (const auto& entry : entries) ordered.push_back(&entry);
        std::sort(ordered.begin(),ordered.end(),[](const Entry* a,const Entry* b) { return a->first<b->first; });
        for (size_t i=1;i<ordered.size();++i)
            if (ordered[i-1]->first==ordered[i]->first) throw std::invalid_argument("duplicate persistent index key");
        return {build_sorted(ordered),entries.size()};
    }
    size_t size() const noexcept { return size_; }
    bool empty() const noexcept { return size_==0; }
    const T* find(uintptr_t key) const noexcept {
        const auto* leaf=leaf_for(root_.get(),key);
        return leaf && leaf->value->first==key ? &leaf->value->second : nullptr;
    }
    PersistentIndex set(uintptr_t key,const T& value) const {
        const auto* old=leaf_for(root_.get(),key);
        if (old && old->value->first==key) return {replace(root_,key,value),size_};
        auto leaf=std::make_shared<const Node>(Entry{key,value});
        if (!old) return {std::move(leaf),1};
        const unsigned bit=std::bit_width(key ^ old->value->first)-1;
        return {insert_at(root_,key,bit,leaf),size_+1};
    }
    PersistentIndex erase(uintptr_t key) const {
        bool removed=false;
        auto next=erase_node(root_,key,removed);
        return {std::move(next),size_-(removed?1:0)};
    }
    class ConstIterator {
        std::array<const Node*,key_bits> pending_{};
        size_t depth_=0;
        const Node* root_=nullptr;
        const Node* current_=nullptr;
        void descend(const Node* node) noexcept {
            while (node && !node->leaf()) {
                assert(depth_<key_bits);
                pending_[depth_++]=node->one.get();
                node=node->zero.get();
            }
            current_=node;
        }
    public:
        using iterator_category=std::forward_iterator_tag;
        using iterator_concept=std::forward_iterator_tag;
        using value_type=Entry;
        using difference_type=std::ptrdiff_t;
        using pointer=const Entry*;
        using reference=const Entry&;
        ConstIterator() noexcept = default;
        explicit ConstIterator(const Node* root,bool at_end=false) noexcept : root_(root) {
            if (!at_end) descend(root);
        }
        reference operator*() const noexcept { assert(current_); return *current_->value; }
        pointer operator->() const noexcept { return &operator*(); }
        ConstIterator& operator++() noexcept {
            assert(current_); descend(depth_?pending_[--depth_]:nullptr); return *this;
        }
        ConstIterator operator++(int) noexcept { auto old=*this; ++*this; return old; }
        friend bool operator==(const ConstIterator& a,const ConstIterator& b) noexcept {
            return a.root_==b.root_ && a.current_==b.current_;
        }
    };
    // Borrowed view: retain this index owner for the entire iteration.
    class EntriesView {
        const Node* root_;
        size_t size_;
    public:
        EntriesView(const Node* root,size_t size) noexcept : root_(root),size_(size) {}
        ConstIterator begin() const noexcept { return ConstIterator(root_); }
        ConstIterator end() const noexcept { return ConstIterator(root_,true); }
        size_t size() const noexcept { return size_; }
        bool empty() const noexcept { return !size_; }
    };
    EntriesView entries() const noexcept { return {root_.get(),size_}; }
};
}
