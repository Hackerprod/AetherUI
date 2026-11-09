#pragma once
#include "node_identity.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace aether::dom {

// Internal input observations, not a style revision, state ledger, or stability
// certificate. C1 has no consumer in the resolver and cannot authorize reuse.
enum class MutationKind : uint8_t { unknown, attribute, text, structure, state };
enum class JournalLoss : uint8_t { none, unknown_mutation, overflow, epoch_gap };

struct MutationRecord {
    // Retain the authentic token, not the node. Destroy/reuse cannot alias it.
    std::shared_ptr<const node_identity_s> subject;
    uint64_t mutation_epoch = 0;
    uint64_t structure_epoch = 0;
    MutationKind kind = MutationKind::unknown;
    int32_t old_state = 0;
    int32_t new_state = 0;
};

class MutationJournal final {
public:
    static constexpr size_t capacity = 128;

    // Fixed storage deliberately makes append allocation-free, including the
    // first mutation and the loss path. There is no journal-allocation OOM.
    void append(const std::shared_ptr<node_identity_s>& subject, MutationKind kind,
                uint64_t epoch, uint64_t structure_epoch,
                int32_t old_state, int32_t new_state) noexcept {
        if (epoch == 0 || epoch != last_epoch_ + 1) lose(JournalLoss::epoch_gap);
        last_epoch_ = epoch;
        if (kind == MutationKind::unknown) lose(JournalLoss::unknown_mutation);
        if (size_ == capacity) {
            lose(JournalLoss::overflow);
            return;
        }
        records_[size_++] = {subject, epoch, structure_epoch, kind, old_state, new_state};
    }

    size_t size() const noexcept { return size_; }
    const MutationRecord* begin() const noexcept { return records_.data(); }
    const MutationRecord* end() const noexcept { return begin() + size_; }
    uint64_t last_epoch() const noexcept { return last_epoch_; }
    JournalLoss loss() const noexcept { return loss_; }
    // This is only recording coverage. False never certifies a style fast path:
    // first resolve, tree/environment/sheet changes and non-state records still
    // require the existing full path. No public reset can hide undeclared input.
    bool coverage_lost(uint64_t current_epoch) const noexcept {
        return loss_ != JournalLoss::none || current_epoch != last_epoch_;
    }

private:
    void lose(JournalLoss loss) noexcept {
        if (loss_ == JournalLoss::none) loss_ = loss;
    }
    std::array<MutationRecord, capacity> records_{};
    size_t size_ = 0;
    uint64_t last_epoch_ = 0;
    JournalLoss loss_ = JournalLoss::none;
};

} // namespace aether::dom
