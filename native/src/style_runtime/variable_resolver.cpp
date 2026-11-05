#include "style_runtime_types.h"
#include "css_variable_tokens.h"
#include <array>
#include <cstring>
#include <memory>

bool custom_property_scope_s::try_get(const std::string& name, std::string& value) const {
    auto it = local.find(name);
    if (it != local.end()) {
        value = it->second;
        return true;
    }
    if (parent) {
        return parent->try_get(name, value);
    }
    value.clear();
    return false;
}

namespace {
using kind = css_variable_token_kind;
struct span { const char* data = nullptr; size_t size = 0; };
struct payload {
    struct block { std::unique_ptr<char[]> bytes; size_t capacity = 0, used = 0; };
    std::vector<block> blocks;
    size_t size = 0;
    bool append(span source, size_t limit, css_variable_budget& budget) {
        if (size > limit || source.size > limit - size) return false;
        size_t cursor = 0;
        while (cursor < source.size) {
            if (blocks.empty() || blocks.back().used == blocks.back().capacity) {
                const size_t capacity = std::min(size_t(256), source.size - cursor);
                if (!budget.take(capacity)) return false;
                blocks.push_back({std::make_unique<char[]>(capacity), capacity, 0});
            }
            auto& b = blocks.back();
            const size_t count = std::min(b.capacity - b.used, source.size - cursor);
            // Copying reads borrowed/owned input and writes the destination.
            if (!budget.take(count) || !budget.take(count)) return false;
            std::memcpy(b.bytes.get() + b.used, source.data + cursor, count);
            b.used += count;
            size += count;
            cursor += count;
        }
        return true;
    }
    bool append(const payload& source, size_t limit, css_variable_budget& budget) {
        if (size > limit || source.size > limit - size) return false;
        for (const auto& b : source.blocks)
            if (!append({b.bytes.get(), b.used}, limit, budget)) return false;
        return true;
    }
    bool compact(std::string& out, css_variable_budget& budget) const {
        // One contiguous exception for lookup keys and final output. Logical
        // capacity includes the terminator; allocator/SSO overhead is not a RSS bound.
        if (!budget.take(size) || !budget.take(1) || !budget.take(1)) return false;
        out.reserve(size);
        for (const auto& b : blocks) {
            if (!budget.take(b.used) || !budget.take(b.used) || !budget.take(1)) return false;
            out.append(b.bytes.get(), b.used);
        }
        return true;
    }
};
// Raw acquisition borrows stable map entries; it never invokes the copying helper.
const std::string* lookup(const custom_property_scope_s* scope, const std::string& name,
                          css_variable_budget& budget) {
    for (auto current = scope; current;) {
        if (!budget.take(1) || !budget.take(name.size())) return nullptr;
        const auto it = current->local.find(name);
        if (it != current->local.end()) {
            const auto& invalid = custom_property_invalid_value();
            if (it->second.size() == invalid.size()) {
                if (!budget.take(invalid.size()) || !budget.take(invalid.size())) return nullptr;
                if (std::memcmp(it->second.data(), invalid.data(), invalid.size()) == 0) return nullptr;
            }
            return &it->second;
        }
        if (!budget.take(1)) return nullptr;
        current = current->parent;
    }
    return nullptr;
}
struct invocation {
    span name, fallback;
    size_t end = 0;
    bool has_comma = false;
};
char closing(char c) { return c == '(' ? ')' : c == '[' ? ']' : '}'; }
bool parse_invocation(span source, size_t body, invocation& call, css_variable_budget& budget) {
    css_variable_lexer lexer(source.data, source.size, body, &budget);
    std::vector<char> blocks;
    bool got_name = false, extra_name = false;
    size_t comma = 0;
    for (;;) {
        const auto t = lexer.next();
        if (budget.exhausted || t.kind == kind::error || t.kind == kind::end) return false;
        if (blocks.empty() && t.kind == kind::close && t.delimiter == ')') {
            call.end = t.end;
            if (call.has_comma) call.fallback = {source.data + comma, t.begin - comma};
            return got_name && !extra_name;
        }
        if (blocks.empty() && t.kind == kind::comma && !call.has_comma) {
            call.has_comma = true;
            comma = t.end;
            continue;
        }
        if (!call.has_comma && blocks.empty() && t.kind != kind::whitespace && t.kind != kind::comment) {
            if (!got_name && t.kind == kind::ident && t.dashed) {
                size_t name_end = t.end;
                // Keep the established raw-key trim, including an escape terminator.
                while (name_end > t.begin) {
                    if (!budget.take(1)) return false;
                    const char c = source.data[name_end - 1];
                    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
                    --name_end;
                }
                call.name = {source.data + t.begin, name_end - t.begin};
                got_name = true;
            } else extra_name = true;
        }
        if (t.kind == kind::open || t.kind == kind::function) {
            if (!budget.take(1)) return false;
            blocks.push_back(closing(t.delimiter));
        } else if (t.kind == kind::close) {
            if (blocks.empty() || blocks.back() != t.delimiter) return false;
            blocks.pop_back();
        }
    }
}
struct frame {
    span source;
    size_t cursor = 0;
    size_t depth = 0;
    payload output;
    std::vector<char> blocks;
    enum class state { scanning, raw, fallback, failed } phase = state::scanning;
    invocation pending;
    // An active entry owns the name. This pointer is not a source backing and remains
    // stable across unordered_map rehash and frame vector relocation.
    const std::string* active_name = nullptr;
    bool cyclic = false;
};
bool resolve(span root, const custom_property_scope_s* scope, int32_t max_depth,
             size_t limit, css_variable_budget& budget, std::string& result) {
    if (max_depth < 0) return false;
    std::vector<frame> frames;
    std::unordered_map<std::string, size_t> active;
    if (!budget.take(1)) return false;
    frames.push_back(frame{root});
    while (!frames.empty() && !budget.exhausted) {
        const size_t index = frames.size() - 1;
        if (frames[index].phase == frame::state::failed || frames[index].cursor == frames[index].source.size) {
            bool ok = frames[index].phase != frame::state::failed && !frames[index].cyclic && frames[index].blocks.empty();
            if (frames[index].active_name) {
                const auto* key = frames[index].active_name;
                frames[index].active_name = nullptr;
                if (!budget.take(1) || !budget.take(key->size())) return false;
                active.erase(*key);
            }
            payload output = std::move(frames[index].output);
            frames.pop_back();
            if (frames.empty()) return ok && output.compact(result, budget);
            const size_t parent = frames.size() - 1;
            if (ok) ok = frames[parent].output.append(output, limit, budget);
            if (budget.exhausted) return false;
            if (ok) { frames[parent].phase = frame::state::scanning; continue; }
            if (frames[parent].phase == frame::state::raw && !frames[parent].cyclic && frames[parent].pending.has_comma && frames[parent].depth < static_cast<size_t>(max_depth)) {
                const auto fallback = frames[parent].pending.fallback;
                const size_t depth = frames[parent].depth + 1;
                frames[parent].phase = frame::state::fallback;
                if (!budget.take(1)) return false;
                frames.push_back(frame{fallback, 0, depth});
                continue;
            }
            frames[parent].phase = frame::state::failed;
            continue;
        }
        const span source = frames[index].source;
        css_variable_lexer lexer(source.data, source.size, frames[index].cursor, &budget);
        const auto t = lexer.next();
        if (budget.exhausted) return false;
        if (t.kind == kind::error || t.kind == kind::end) { frames[index].phase = frame::state::failed; continue; }
        frames[index].cursor = t.end;
        if (t.kind != kind::function || !t.is_var) {
            if (t.kind == kind::open || t.kind == kind::function) {
                if (!budget.take(1)) return false;
                frames[index].blocks.push_back(closing(t.delimiter));
            } else if (t.kind == kind::close) {
                if (frames[index].blocks.empty() || frames[index].blocks.back() != t.delimiter) {
                    frames[index].phase = frame::state::failed;
                    continue;
                }
                frames[index].blocks.pop_back();
            }
            if (!frames[index].output.append({source.data + t.begin, t.end - t.begin}, limit, budget)) frames[index].phase = frame::state::failed;
            continue;
        }
        invocation call;
        if (!parse_invocation(source, t.end, call, budget)) { frames[index].phase = frame::state::failed; continue; }
        frames[index].cursor = call.end;
        frames[index].pending = call;
        payload name_bytes;
        std::string name;
        if (!name_bytes.append(call.name, call.name.size, budget) || !name_bytes.compact(name, budget)) return false;
        if (!budget.take(1) || !budget.take(name.size())) return false;
        const auto found = active.find(name);
        const std::string* raw = nullptr;
        if (found != active.end()) {
            // Mark only the active cyclic segment. Unmarked callers may recover.
            for (size_t i = found->second; i < frames.size(); ++i) {
                if (!budget.take(1)) return false;
                frames[i].cyclic = true;
            }
        } else {
            raw = lookup(scope, name, budget);
        }
        if (budget.exhausted) return false;
        if (raw && frames[index].depth < static_cast<size_t>(max_depth)) {
            const size_t depth = frames[index].depth + 1;
            frames[index].phase = frame::state::raw;
            // Conservative charge for hash, owned capacity and possible SSO move copy.
            if (!budget.take(1) || !budget.take(name.size()) ||
                !budget.take(name.size()) || !budget.take(1) ||
                !budget.take(name.size()) || !budget.take(name.size())) return false;
            const auto inserted = active.emplace(std::move(name), frames.size());
            const auto* key = &inserted.first->first;
            const span raw_span{raw->data(), raw->size()};
            if (!budget.take(1)) return false;
            frames.push_back(frame{raw_span, 0, depth});
            frames.back().active_name = key;
        } else if (!frames[index].cyclic && call.has_comma && frames[index].depth < static_cast<size_t>(max_depth)) {
            const size_t depth = frames[index].depth + 1;
            frames[index].phase = frame::state::fallback;
            if (!budget.take(1)) return false;
            frames.push_back(frame{call.fallback, 0, depth});
        } else frames[index].phase = frame::state::failed;
    }
    return false;
}
}

bool variable_resolve(const char* source, const custom_property_scope_s* scope,
                      int32_t max_depth, int32_t max_expansion_length, std::string& out_value) {
    bool ok = false;
    std::string result;
    {
        // Keep out_value untouched until all borrowed root/map spans are destroyed.
        css_variable_budget budget;
        if (source && max_expansion_length >= 0) {
            size_t length = 0;
            while (budget.take(1)) {
                if (source[length] == '\0') {
                    ok = resolve({source, length}, scope, max_depth,
                                 static_cast<size_t>(max_expansion_length), budget, result);
                    // Publishing the compact result may copy SSO payload on move.
                    if (ok && (!budget.take(1) || !budget.take(result.size()) ||
                               !budget.take(1) || !budget.take(result.size()) ||
                               !budget.take(result.size()))) ok = false;
                    break;
                }
                ++length;
            }
        }
    }
    if (ok) out_value = std::move(result);
    else out_value.clear();
    return ok;
}
