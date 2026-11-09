#include "../diagnostics/work_counters.h"
using aether::diagnostics::count_work;
using aether::diagnostics::WorkCounter;
#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include "../css/computed_style_handle.h"
#include "../css/measure_state.h"
#include <stdexcept>
#include <thread>
#include "../abi/aether_abi.h"
#include "../css/border_utils.h"
#include "../css/computed_style_internal.h"
#include "../css/style_map_types.h"
#include "../dom/dom_types.h"
#include "../primitives/html_space_tokens.h"
#include "style_property_registry_internal.h"
#include "style_runtime_types.h"
#include "style_source_limits.h"
#include "state_invalidation.h"
#include "state_reconciliation.h"
#include "state_semantics.h"
#include "css_variable_tokens.h"
#include "custom_property_scope_internal.h"
#include "frozen_runtime_metadata.h"
#include "../css/style_revision.h"
#include "../css/frozen_measure_read.h"
#include "../css/style_map_version.h"
using namespace aether::style;

#include <algorithm>
#include <cctype>
#include <cstring>
#include <memory>
#include <map>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>


// Private preparation data, never a public handle or final record.
struct PreparedNodeResult {
    FrozenStyleRef style;
    std::shared_ptr<measure_state_s> measures;
    FrozenScopeRef custom_properties;
    // Separate backdrop terminals are preserved in the frozen node payload.
    FrozenScopeRef backdrop_custom_properties;
    FrozenColorRef color_references;
    uint32_t declared_impact=0, declared_inherited_impact=0, inherited_change_impact=0, impact=0;
    uint32_t parent_dependencies=ParentDependencies::all;
};
struct MutableStyleBuilder {
    aether_computed_style_t value{};
    MutableStyleBuilder() = default;
    MutableStyleBuilder(const MutableStyleBuilder&)=delete;
    MutableStyleBuilder& operator=(const MutableStyleBuilder&)=delete;
    MutableStyleBuilder(MutableStyleBuilder&& other) noexcept : value(other.value) { other.value={}; }
    MutableStyleBuilder& operator=(MutableStyleBuilder&& other) noexcept {
        if(this!=&other){aether_computed_style_free(&value);value=other.value;other.value={};}
        return *this;
    }
    ~MutableStyleBuilder(){aether_computed_style_free(&value);}
};
static void require_style_status(aether_status_t status) {
    if(status==AETHER_ERROR_OUT_OF_MEMORY)throw std::bad_alloc();
    if(status!=AETHER_OK)throw std::invalid_argument("invalid frozen style preparation");
}
static color_resolution_state_s copy_color_state(const FrozenColorMetadata& source) {
    color_resolution_state_s result;
    result.background_uses_current_color=source.background();
    for(size_t i=0;i<4;++i)result.border_uses_current_color[i]=source.border(i);
    for(size_t i=0;i<source.text_shadow_count();++i)result.text_shadow_uses_current_color.push_back(source.text_shadow_at(i));
    for(size_t i=0;i<source.box_shadow_count();++i)result.box_shadow_uses_current_color.push_back(source.box_shadow_at(i));
    return result;
}
static std::vector<bool> copy_text_color_flags(const FrozenColorMetadata& source) {
    std::vector<bool> out; out.reserve(source.text_shadow_count());
    for(size_t i=0;i<source.text_shadow_count();++i)out.push_back(source.text_shadow_at(i));
    return out;
}
static bool text_color_flags_equal(const FrozenColorMetadata& source,const std::vector<bool>& flags) {
    if(source.text_shadow_count()!=flags.size())return false;
    for(size_t i=0;i<flags.size();++i)if(source.text_shadow_at(i)!=flags[i])return false;
    return true;
}
struct aether_style_update_s {
    aether_node_s* node = nullptr;
    // An update crosses the runtime/binding boundary after the style tree may
    // have been rebuilt.  It therefore owns a deep snapshot rather than
    // borrowing storage from PreparedNodeResult.
    aether_computed_style_handle_s current{};
    uint32_t impact = STYLE_IMPACT_NONE;
    ~aether_style_update_s() { aether_computed_style_free(&current.style); }
};

// The rule index and state programs retain the exact immutable AST owners.
// Transactions share this pin; removing a sheet cannot leave borrowed rule pointers.
struct CompiledStyleInputs {
    std::vector<std::shared_ptr<const compiled_style_sheet_s>> sheets;
    rule_index_s index;
    std::vector<invalidation::Program> state_programs;
};

// O(1) pin of the authoritative persistent revision directory. No second N-node
// cache is copied when a candidate shares untouched payloads with its base.
class RuntimeStateCache {
    StyleRevisionRef revision_;
public:
    RuntimeStateCache()=default;
    explicit RuntimeStateCache(StyleRevisionRef revision):revision_(std::move(revision)){}
    const FrozenNodePayloadRef* find(aether_node_s* node)const noexcept {
        const auto* entry=revision_?revision_->directory().find(reinterpret_cast<uintptr_t>(node)):nullptr;
        if(!entry)return nullptr;
        const auto& token=entry->payload->node_lifetime();
        if(token&&(!token->alive||!token->attached))return nullptr;
        return &entry->payload;
    }
    const FrozenNodePayloadRef& at(aether_node_s* node)const {
        const auto* found=find(node);if(!found)throw std::out_of_range("missing runtime state");return *found;
    }
    size_t count(aether_node_s* node)const noexcept{return find(node)?1:0;}
    size_t size()const noexcept{return revision_?revision_->directory().size():0;}
    bool empty()const noexcept{return size()==0;}
    void clear()noexcept{revision_.reset();}
    void swap(RuntimeStateCache& other)noexcept{revision_.swap(other.revision_);}
};

struct aether_style_runtime_s {
    aether_style_runtime_options_t options{};
    aether_style_environment_t environment{};
    aether_style_diagnostic_fn diag_cb = nullptr;
    void* diag_user_data = nullptr;
    int64_t next_sheet_handle = 1;
    int32_t next_rule_id = 1;
    std::unordered_map<int64_t, std::shared_ptr<const compiled_style_sheet_s>> sheets;
    RuntimeStateCache states;
    std::unordered_map<aether_node_s*, std::unique_ptr<PreparedNodeResult>> prepared;
    std::shared_ptr<document_measure_context_s> document_measures;
    std::shared_ptr<const style_revision_s> revision;
    std::shared_ptr<const CompiledStyleInputs> revision_compiled;
    std::shared_ptr<const style_property_registry_s> registry;
    std::shared_ptr<const style_property_registry_s> revision_registry;
    uint64_t generation = 0;
    uint64_t sheet_epoch = 0;
    uint64_t revision_sheet_epoch = 0;
    bool preparing = false;
    aether_node_s* legacy_inheritance_root = nullptr; // Candidate-only compatibility boundary.
    std::thread::id owner_thread = std::this_thread::get_id();
    std::shared_ptr<const CompiledStyleInputs> compiled = std::make_shared<const CompiledStyleInputs>();
    static std::shared_ptr<const CompiledStyleInputs> prepare_rule_index(const aether_style_runtime_s* runtime,
        std::shared_ptr<const compiled_style_sheet_s> extra = {}, int64_t omit = 0);
};

struct aether_style_revision_handle_s { std::shared_ptr<const style_revision_s> value; };

// An add must finish its transaction before invoking consumer code. Each call
// owns its batch and captures its recipient, so callbacks may add/remove sheets
// without invalidating candidate handles/IDs or redirecting an existing batch.
struct stylesheet_diagnostics_s {
    struct message_s {
        std::optional<std::string> code, property, value, message;
    };
    aether_style_diagnostic_fn callback;
    void* user_data;
    std::vector<message_s> messages;

    stylesheet_diagnostics_s(aether_style_diagnostic_fn fn, void* data)
        : callback(fn), user_data(data) {}

    static std::optional<std::string> copy(const char* text) {
        return text ? std::optional<std::string>(text) : std::nullopt;
    }
    static void collect(const char* code, const char* property, const char* value,
                        const char* message, void* data) {
        auto& batch = *static_cast<stylesheet_diagnostics_s*>(data);
        batch.messages.push_back({copy(code), copy(property), copy(value), copy(message)});
    }
    aether_style_diagnostic_fn collector() const noexcept {
        return callback ? collect : nullptr;
    }
    void replay() const {
        if (!callback) return;
        for (const auto& entry : messages) {
            callback(entry.code ? entry.code->c_str() : nullptr,
                     entry.property ? entry.property->c_str() : nullptr,
                     entry.value ? entry.value->c_str() : nullptr,
                     entry.message ? entry.message->c_str() : nullptr, user_data);
        }
    }
};

static constexpr uint32_t STYLE_IMPACT_ALL_BITS =
    static_cast<uint32_t>(STYLE_IMPACT_LAYOUT) |
    static_cast<uint32_t>(STYLE_IMPACT_PAINT) |
    static_cast<uint32_t>(STYLE_IMPACT_HITTEST) |
    static_cast<uint32_t>(STYLE_IMPACT_COMPOSITE) |
    static_cast<uint32_t>(STYLE_IMPACT_TEXT_LAYOUT) |
    static_cast<uint32_t>(STYLE_IMPACT_CURSOR) |
    static_cast<uint32_t>(STYLE_IMPACT_INHERITED);

static bool is_calc_expression(const char* value) {
    if (!value) return false;
    while (*value && std::isspace(static_cast<unsigned char>(*value))) ++value;
    static constexpr char prefix[] = "calc(";
    for (size_t index = 0; index < sizeof(prefix) - 1; ++index) {
        const char current = value[index];
        if (!current) return false;
        const char lowered = current >= 'A' && current <= 'Z'
            ? static_cast<char>(current - 'A' + 'a') : current;
        if (lowered != prefix[index]) return false;
    }
    return true;
}

static node_style_info_s get_node_info(aether_node_s* node) {
    node_style_info_s info;
    info.type_name = node->tag_name;
    if (auto it = node->attributes.find("id"); it != node->attributes.end())
        info.id = it->second;
    if (auto it = node->attributes.find("class"); it != node->attributes.end()) {
        aether::html_space_token_scanner scanner(it->second);
        std::string_view token;
        while (scanner.next(token))
            info.classes.emplace_back(token); // Own the bytes beyond this scan.
    }
    return info;
}

static bool color_equal(const aether_color_rgba& a, const aether_color_rgba& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static bool length_equal(const aether_css_length& a, const aether_css_length& b) {
    return a.value == b.value &&
           a.unit == b.unit &&
           a.linear_percent == b.linear_percent &&
           a.linear_em == b.linear_em &&
           a.linear_rem == b.linear_rem &&
           a.linear_vw == b.linear_vw &&
           a.linear_vh == b.linear_vh;
}

static bool edges_equal(const aether_css_edges& a, const aether_css_edges& b) {
    return length_equal(a.left, b.left) &&
           length_equal(a.top, b.top) &&
           length_equal(a.right, b.right) &&
           length_equal(a.bottom, b.bottom);
}

static bool color_edges_equal(const aether_color_edges& a, const aether_color_edges& b) {
    return color_equal(a.left, b.left) &&
           color_equal(a.top, b.top) &&
           color_equal(a.right, b.right) &&
           color_equal(a.bottom, b.bottom);
}

static bool border_style_edges_equal(const aether_border_style_edges& a, const aether_border_style_edges& b) {
    return a.left == b.left &&
           a.top == b.top &&
           a.right == b.right &&
           a.bottom == b.bottom;
}

static bool flex_basis_equal(const aether_flex_basis& a, const aether_flex_basis& b) {
    return a.kind == b.kind && length_equal(a.length, b.length);
}

static bool corner_radius_equal(const aether_corner_radius& a, const aether_corner_radius& b) {
    return length_equal(a.horizontal, b.horizontal) &&
           length_equal(a.vertical, b.vertical);
}

static bool border_radii_equal(const aether_border_radii& a, const aether_border_radii& b) {
    return corner_radius_equal(a.top_left, b.top_left) &&
           corner_radius_equal(a.top_right, b.top_right) &&
           corner_radius_equal(a.bottom_right, b.bottom_right) &&
           corner_radius_equal(a.bottom_left, b.bottom_left);
}

static bool transform_origin_equal(const aether_transform_origin& a, const aether_transform_origin& b) {
    return length_equal(a.x, b.x) && length_equal(a.y, b.y);
}

static bool gradient_stop_equal(const aether_gradient_stop& a, const aether_gradient_stop& b) {
    return color_equal(a.color, b.color) &&
           length_equal(a.position, b.position) &&
           a.offset == b.offset &&
           a.has_position == b.has_position;
}

template<class T> static bool frozen_array_equal(ReadOnlyArrayView<T> a,const T* b,int32_t n) {
    if(n<0||a.size()!=static_cast<size_t>(n)||(n&&!b))return false;
    for(size_t i=0;i<a.size();++i)if(std::memcmp(&a[i],&b[i],sizeof(T))!=0)return false;
    return true;
}
static bool frozen_gradient_equal(GradientReadView a,const aether_linear_gradient& b,bool bytes) {
    if(a.direction_x!=b.direction_x||a.direction_y!=b.direction_y||b.stop_count<0||a.stops.size()!=static_cast<size_t>(b.stop_count)||(b.stop_count&&!b.stops))return false;
    for(size_t i=0;i<a.stops.size();++i)if(bytes ? std::memcmp(&a.stops[i],&b.stops[i],sizeof(aether_gradient_stop))!=0 : !gradient_stop_equal(a.stops[i],b.stops[i]))return false;
    return true;
}
static bool backgrounds_equal(ReadOnlyArrayView<FrozenBackgroundLayer> a,const aether_background_layer_list& b) {
    if(b.count<0||a.size()!=static_cast<size_t>(b.count)||(b.count&&!b.items))return false;
    for(size_t i=0;i<a.size();++i) {
        const auto v=a[i].view(); const auto& x=v.scalars; const auto& y=b.items[i];
        if(x.kind!=y.kind||std::memcmp(&x.position_x,&y.position_x,sizeof(aether_css_length))||std::memcmp(&x.position_y,&y.position_y,sizeof(aether_css_length))||x.size_kind!=y.size_kind||std::memcmp(&x.size_x,&y.size_x,sizeof(aether_css_length))||std::memcmp(&x.size_y,&y.size_y,sizeof(aether_css_length))||x.repeat_x!=y.repeat_x||x.repeat_y!=y.repeat_y)return false;
        if(x.kind==AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT){if(!frozen_gradient_equal(v.gradient,y.gradient,true))return false;}
        else if(x.kind==AETHER_BACKGROUND_LAYER_IMAGE_URL){if(!v.image_source.present()||!y.image_source||std::strcmp(v.image_source.c_str(),y.image_source))return false;}
        else return false;
    }
    return true;
}
static bool complete_background_payload_equal(ComputedStyleReadView a,const aether_computed_style_t& b) {
    const auto layers=a.background_layers();
    for(size_t i=0;i<layers.size();++i){auto x=layers[i].view();const auto& y=b.background_layers.items[i];
        if(!frozen_gradient_equal(x.gradient,y.gradient,true))return false;
        if(x.image_source.present()!=(y.image_source!=nullptr))return false;
        if(y.image_source&&!x.image_source.equals(y.image_source))return false;
    }
    return true;
}
static bool style_equal(ComputedStyleReadView av, const aether_computed_style_t& b) {
    const auto& a=av.scalars();
    return a.display == b.display &&
           a.flex_direction == b.flex_direction &&
           a.justify_content == b.justify_content &&
           a.align_items == b.align_items &&
           a.align_self == b.align_self &&
           a.flex_grow == b.flex_grow &&
           a.flex_shrink == b.flex_shrink &&
           flex_basis_equal(a.flex_basis, b.flex_basis) &&
           a.flex_wrap == b.flex_wrap &&
           a.gap == b.gap &&
           a.position == b.position &&
           edges_equal(a.inset, b.inset) &&
           length_equal(a.width, b.width) &&
           length_equal(a.height, b.height) &&
           length_equal(a.min_width, b.min_width) &&
           length_equal(a.min_height, b.min_height) &&
           length_equal(a.max_width, b.max_width) &&
           length_equal(a.max_height, b.max_height) &&
           edges_equal(a.margin, b.margin) &&
           edges_equal(a.padding, b.padding) &&
           edges_equal(a.border_width, b.border_width) &&
           a.box_sizing == b.box_sizing &&
           color_equal(a.color, b.color) &&
           color_equal(a.background_color, b.background_color) &&
           backgrounds_equal(av.background_layers(),b.background_layers) &&
           frozen_array_equal(av.backdrop_filters(),b.backdrop_filters.items,b.backdrop_filters.count) &&
           color_equal(a.border_color, b.border_color) &&
           color_edges_equal(a.border_colors, b.border_colors) &&
           border_style_edges_equal(a.border_styles, b.border_styles) &&
           a.has_text_gradient == b.has_text_gradient &&
           frozen_gradient_equal(av.text_gradient(), b.text_gradient, false) &&
           frozen_array_equal(av.transforms(),b.transforms.items,b.transforms.count) &&
           transform_origin_equal(a.transform_origin, b.transform_origin) &&
           frozen_array_equal(av.text_shadows(),b.text_shadows.items,b.text_shadows.count) &&
           frozen_array_equal(av.box_shadows(),b.box_shadows.items,b.box_shadows.count) &&
           border_radii_equal(a.border_radii, b.border_radii) &&
           a.opacity == b.opacity &&
           std::strncmp(a.font_family.data(), b.font_family, sizeof(a.font_family)) == 0 &&
           a.font_size == b.font_size &&
           a.font_weight == b.font_weight &&
           a.line_height == b.line_height &&
           a.text_align == b.text_align &&
           a.text_transform == b.text_transform &&
           a.white_space == b.white_space &&
           a.overflow_x == b.overflow_x &&
           a.overflow_y == b.overflow_y &&
           a.cursor == b.cursor &&
           a.pointer_events == b.pointer_events &&
            a.visible == b.visible &&
            a.z_index == b.z_index &&
            a.z_index_kind == b.z_index_kind &&
            a.transition_properties == b.transition_properties &&
            a.transition_duration == b.transition_duration &&
            a.transition_delay == b.transition_delay &&
            a.transition_timing == b.transition_timing;
}

std::shared_ptr<const CompiledStyleInputs> aether_style_runtime_s::prepare_rule_index(
    const aether_style_runtime_s* runtime, std::shared_ptr<const compiled_style_sheet_s> extra, int64_t omit) {
    auto result = std::make_shared<CompiledStyleInputs>();
    auto& owners = result->sheets;
    owners.reserve(runtime->sheets.size() + (extra ? 1 : 0));
    for (const auto& pair : runtime->sheets) if (pair.first != omit) owners.push_back(pair.second);
    if (extra) owners.push_back(std::move(extra));
    std::vector<const compiled_style_sheet_s*> sheets;
    sheets.reserve(owners.size());
    for (const auto& owner : owners) sheets.push_back(owner.get());
    auto& prepared = result->index;
    rule_index_rebuild(&prepared, sheets);
    // No match/media/state/content filtering: even a content-free pseudo rule
    // can produce diagnostics. Unknown future kinds force the full path.
    bool complete = true;
    for (const auto* sheet : sheets) for (const auto& rule : sheet->rules) {
        result->state_programs.push_back(invalidation::compile(rule.rule_id, rule.selector));
        switch (rule.selector.pseudo_element) {
        case aether_pseudo_element_t::none: break;
        case aether_pseudo_element_t::before: prepared.before_possible_ = true; break;
        case aether_pseudo_element_t::after: prepared.after_possible_ = true; break;
        default: complete = false; break;
        }
    }
    prepared.pseudo_capabilities_complete_ = complete;
    return result;
}

static void report(aether_style_runtime_s* runtime, const char* code,
                   const char* property, const char* value, const char* message) {
    if (runtime->diag_cb)
        runtime->diag_cb(code, property, value, message, runtime->diag_user_data);
}

static bool is_color_producer(const style_property_definition_s& definition) {
    // The current model maps text-fill-color onto the same concrete text color.
    // Resolve every such producer before references to currentColor are fixed.
    return definition.name == "color" ||
           definition.name == "-webkit-text-fill-color" ||
           definition.name == "text-fill-color";
}

static void resolve_symbolic_colors(aether_computed_style_t& style,
                                    const color_resolution_state_s& references) {
    if (references.background_uses_current_color)
        style.background_color = style.color;
    if (references.border_uses_current_color[static_cast<int>(aether_border_side_t::top)])
        style.border_colors.top = style.color;
    if (references.border_uses_current_color[static_cast<int>(aether_border_side_t::right)])
        style.border_colors.right = style.color;
    if (references.border_uses_current_color[static_cast<int>(aether_border_side_t::bottom)])
        style.border_colors.bottom = style.color;
    if (references.border_uses_current_color[static_cast<int>(aether_border_side_t::left)])
        style.border_colors.left = style.color;

    for (int32_t index = 0; index < style.box_shadows.count; ++index) {
        if (index < static_cast<int32_t>(references.box_shadow_uses_current_color.size()) &&
            references.box_shadow_uses_current_color[static_cast<size_t>(index)]) {
            style.box_shadows.items[index].color = style.color;
        }
    }
    for (int32_t index = 0; index < style.text_shadows.count; ++index) {
        if (index < static_cast<int32_t>(references.text_shadow_uses_current_color.size()) &&
            references.text_shadow_uses_current_color[static_cast<size_t>(index)]) {
            style.text_shadows.items[index].color = style.color;
        }
    }

    // Legacy bindings expose this representative in addition to the per-edge
    // colors. Define it deterministically rather than leaking apply order.
    style.border_color = style.border_colors.left;
}

static bool overflow_is_visible(int32_t value) {
    return value == AETHER_OVERFLOW_VISIBLE;
}

static bool overflow_is_clip(int32_t value) {
    return value == AETHER_OVERFLOW_CLIP;
}

static void normalize_overflow_axes(aether_computed_style_t& style) {
    // CSS Overflow 3.1 computed-value rule. An axis cannot be visible (or clip)
    // while the opposite axis clips or scrolls. Visible -> auto when the other
    // axis is neither visible nor clip; clip -> hidden when the other axis is
    // neither visible nor clip. Normalizing after the cascade keeps all layout
    // and paint consumers on the same computed values.
    if (overflow_is_visible(style.overflow_x) && !overflow_is_visible(style.overflow_y) &&
        !overflow_is_clip(style.overflow_y))
        style.overflow_x = AETHER_OVERFLOW_AUTO;
    if (overflow_is_visible(style.overflow_y) && !overflow_is_visible(style.overflow_x) &&
        !overflow_is_clip(style.overflow_x))
        style.overflow_y = AETHER_OVERFLOW_AUTO;
    if (overflow_is_clip(style.overflow_x) && !overflow_is_visible(style.overflow_y) &&
        !overflow_is_clip(style.overflow_y))
        style.overflow_x = AETHER_OVERFLOW_HIDDEN;
    if (overflow_is_clip(style.overflow_y) && !overflow_is_visible(style.overflow_x) &&
        !overflow_is_clip(style.overflow_x))
        style.overflow_y = AETHER_OVERFLOW_HIDDEN;
}

static bool try_parse_generated_content(const std::string& source, std::string& output) {
    std::string value = str_trim(source);
    if (value.empty() || str_iequals(value, "none") || str_iequals(value, "normal")) return false;

    output.clear();
    size_t index = 0;
    bool found_string = false;
    while (index < value.size()) {
        while (index < value.size() && std::isspace(static_cast<unsigned char>(value[index]))) ++index;
        if (index >= value.size()) break;
        const char quote = value[index];
        if (quote != '\'' && quote != '"') return false;
        ++index;
        found_string = true;
        while (index < value.size() && value[index] != quote) {
            if (value[index] == '\\' && index + 1 < value.size()) ++index;
            output += value[index++];
        }
        if (index >= value.size()) return false;
        ++index;
    }
    return found_string;
}

static PreparedNodeResult* compute_node(aether_style_runtime_s* runtime, aether_node_s* node,
                                          PreparedNodeResult* parent, uint32_t inherited_parent_impact,
                                          uint32_t& impact, uint32_t& inherited_change_impact,
                                          std::optional<std::string>* generated_content = nullptr) {
    count_work(WorkCounter::compute_nodes);
    // The legacy subtree entry point starts an isolated inheritance scope.
    // Contextual preparation never installs this compatibility boundary.
    if (node == runtime->legacy_inheritance_root) { parent = nullptr; inherited_parent_impact = 0; }
    const auto* registry = runtime->registry.get();
    const auto parent_view=parent ? std::optional<ComputedStyleReadView>(parent->style->view()) : std::nullopt;
    MutableStyleBuilder next_builder;
    auto& next=next_builder.value;
    if (computed_style_inherit_checked(parent_view ? &*parent_view : nullptr, &next) != AETHER_OK)
        throw std::bad_alloc();
    auto measures = std::make_shared<measure_state_s>();
    measures->document = runtime->document_measures;
    measures->parent_font_size = parent ? parent->style->view().scalars().font_size : measures->document->initial_font_size;
    measures->self_font_size = measures->parent_font_size;
    next.font_size = measures->self_font_size;
    if (parent) measure_set_property_dependencies(*measures, "font-size", MEASURE_PARENT);
    if (parent && parent->measures) {
        measures->line_height = parent->measures->line_height;
        measure_set_property_dependencies(*measures, "line-height", MEASURE_PARENT);
    }
    const bool document_element = !node->is_generated && node->type == AETHER_NODE_ELEMENT &&
        ((!node->parent) || (node->parent->type == AETHER_NODE_DOCUMENT &&
          aether_doc_get_document_element(static_cast<aether_doc_s*>(node->parent)) == node));
    std::vector<cascade_entry_s> cascade(static_cast<size_t>(registry->property_count()));
    struct custom_winner_s {
        cascade_priority_t priority{};
        // Compiled rules and the inline vector own these strings throughout preparation.
        const custom_property_declaration_s* declaration = nullptr;
        void offer(const cascade_priority_t& p, const custom_property_declaration_s& d) {
            if (!declaration || cascade_priority_compare(&p, &priority) >= 0) {
                priority = p;
                declaration = &d;
            }
        }
    };
    std::unordered_map<std::string, custom_winner_s> custom_winners;
    cascade_entry_custom_s content_winner;

    std::vector<const compiled_rule_s*> candidates;
    auto info = get_node_info(node);
    rule_index_get_candidates(&runtime->compiled->index, &info, candidates);
    for (const auto* rule : candidates) {
        if (rule->selector.pseudo_element != node->pseudo_element) continue;
        if (!media_condition_matches(rule->condition, &runtime->environment) ||
            !selector_match(rule->selector, node)) continue;
        for (const auto& declaration : rule->declarations) {
            cascade_priority_t priority{
                declaration.important, false, rule->layer, rule->selector.specificity,
                rule->source_order, declaration.declaration_order
            };
            if (declaration.property_id >= 0 &&
                declaration.property_id < registry->property_count())
                cascade[static_cast<size_t>(declaration.property_id)].offer(priority, declaration.value);
        }
        for (const auto& declaration : rule->custom_declarations) {
            cascade_priority_t priority{
                declaration.important, false, rule->layer, rule->selector.specificity,
                rule->source_order, declaration.declaration_order
            };
            custom_winners[declaration.name].offer(priority, declaration);
        }
        for (const auto& declaration : rule->content_declarations) {
            cascade_priority_t priority{
                declaration.important, false, rule->layer, rule->selector.specificity,
                rule->source_order, declaration.declaration_order
            };
            content_winner.offer(priority, declaration.source);
        }
    }

    std::vector<compiled_property_declaration_s> inline_properties;
    std::vector<custom_property_declaration_s> inline_custom;
    std::vector<compiled_shorthand_declaration_s> inline_shorthands;
    // Generated pseudo-elements retain host attributes for selector matching, but
    // the host's inline declaration block does not cascade onto ::before/::after.
    if (!node->is_generated) if (auto it = node->attributes.find("style"); it != node->attributes.end()) {
        style_source_compile_inline(it->second.c_str(), inline_properties, inline_custom, inline_shorthands,
                                    runtime->diag_cb, runtime->diag_user_data);
        for (const auto& declaration : inline_properties) {
            cascade_priority_t priority{
                declaration.important, true, style_sheet_layer::author,
                specificity_s{1, 0, 0}, INT32_MAX, declaration.declaration_order
            };
            if (declaration.property_id >= 0 &&
                declaration.property_id < registry->property_count())
                cascade[static_cast<size_t>(declaration.property_id)].offer(priority, declaration.value);
        }
        for (const auto& declaration : inline_custom) {
            cascade_priority_t priority{
                declaration.important, true, style_sheet_layer::author,
                specificity_s{1, 0, 0}, INT32_MAX, declaration.declaration_order
            };
            custom_winners[declaration.name].offer(priority, declaration);
        }
    }

    custom_property_sources_s sources;
    bool backdrop_sources_differ = parent && parent->backdrop_custom_properties != parent->custom_properties;
    for (const auto& pair : custom_winners) {
        const auto& declaration = *pair.second.declaration;
        auto value = str_trim(declaration.source);
        if (str_iequals(value, "inherit") || str_iequals(value, "unset")) continue;
        const bool initial = str_iequals(value, "initial") || str_iequals(value, "revert");
        sources.emplace(pair.first, custom_property_source_s{declaration.source, initial});
        if (!initial && declaration.original_source) backdrop_sources_differ = true;
    }

    auto scope = compute_frozen_custom_property_scope(sources, parent ? parent->custom_properties : nullptr,
        runtime->options.max_variable_depth, runtime->options.max_variable_expansion_length);
    // Ordinary nodes share the exact legacy scope without a second source map or
    // resolution. Only effective winning differences (or inherited differences)
    // need a focal view. CSS-wide classification always comes from the legacy winner.
    auto backdrop_scope = scope;
    if (backdrop_sources_differ) {
        custom_property_sources_s backdrop_sources;
        for (const auto& pair : sources) {
            const auto& declaration = *custom_winners.at(pair.first).declaration;
            backdrop_sources.emplace(pair.first, custom_property_source_s{
                !pair.second.initial && declaration.original_source
                    ? *declaration.original_source : pair.second.source, pair.second.initial});
        }
        backdrop_scope = compute_frozen_custom_property_scope(backdrop_sources,
            parent ? parent->backdrop_custom_properties : nullptr,
            runtime->options.max_variable_depth, runtime->options.max_variable_expansion_length);
    }

    // Custom properties have no dependency on the final ordinary style in this
    // runtime. Compile each pending group once per node, then offer either all
    // valid siblings or unset at the ORIGINAL priority. A computed failure must
    // not resurrect lower-priority declarations; other longhand winners survive.
    auto offer_shorthand = [&](const compiled_shorthand_declaration_s& declaration,
                               const cascade_priority_t& priority) {
        const auto* shorthand = registry->try_get_shorthand(declaration.name.c_str());
        if (!shorthand) return;
        std::string resolved;
        std::vector<compiled_property_declaration_s> staged;
        const bool substituted = resolve_frozen_computed_value(declaration.source.c_str(), scope.get(),
            runtime->options.max_variable_depth, runtime->options.max_variable_expansion_length, resolved);
        const bool valid = substituted && validate_pending_declaration_value(resolved) &&
            !variable_has_reference(resolved) &&
            style_source_compile_shorthand(*shorthand, resolved, declaration.important,
                declaration.declaration_order, registry, staged, runtime->diag_cb, runtime->diag_user_data);
        if (valid) {
            for (const auto& item : staged)
                cascade[static_cast<size_t>(item.property_id)].offer(priority, item.value);
        } else {
            report(runtime, substituted ? "AETHER-CSS-VALUE" : "AETHER-CSS-VAR", declaration.name.c_str(),
                   declaration.source.c_str(), substituted ? "Resolved shorthand is invalid." : "Unable to resolve CSS variable.");
            compiled_style_value_s unset;
            unset.mode = compiled_value_mode::unset;
            for (auto id : shorthand->target_ids)
                cascade[static_cast<size_t>(id)].offer(priority, unset);
        }
    };
    for (const auto* rule : candidates) {
        if (rule->selector.pseudo_element != node->pseudo_element) continue;
        if (!media_condition_matches(rule->condition, &runtime->environment) ||
            !selector_match(rule->selector, node)) continue;
        for (const auto& declaration : rule->shorthand_declarations) {
            offer_shorthand(declaration, cascade_priority_t{
                declaration.important, false, rule->layer, rule->selector.specificity,
                rule->source_order, declaration.declaration_order});
        }
    }
    for (const auto& declaration : inline_shorthands) {
        offer_shorthand(declaration, cascade_priority_t{
            declaration.important, true, style_sheet_layer::author,
            specificity_s{1, 0, 0}, INT32_MAX, declaration.declaration_order});
    }

    if (generated_content) {
        generated_content->reset();
        if (content_winner.is_set) {
            std::string content_source = content_winner.source;
            if (variable_has_reference(content_source) &&
                !resolve_frozen_computed_value(content_source.c_str(), scope.get(),
                                  runtime->options.max_variable_depth,
                                  runtime->options.max_variable_expansion_length, content_source)) {
                report(runtime, "AETHER-CSS-VAR", "content", content_winner.source.c_str(),
                       "Unable to resolve CSS variable.");
            } else {
                std::string parsed_content;
                if (try_parse_generated_content(content_source, parsed_content)) {
                    *generated_content = std::move(parsed_content);
                }
            }
        }
    }

    const auto* found = runtime->states.find(node);
    if(found)count_work(WorkCounter::state_entries_visited);
    const auto previous_style = !found ? std::optional<ComputedStyleReadView>{} : std::optional<ComputedStyleReadView>{(*found)->style_view()};

    auto apply_property_value = [&](const style_property_definition_s& definition,
                                    aether_computed_style_t& style,
                                    color_resolution_state_s& references,
                                    const std::string& value,
                                    bool emit_diagnostics,
                                    measure_apply_state_s& destination) {
        destination.set_property_dependencies(measure_source_dependencies(value));
        property_apply_context_s context{&style, &references, &destination};
        property_apply_result_s apply_result{};
        if (definition.apply_with_context_checked) {
            apply_result = definition.apply_with_context_checked(context, value);
        } else if (definition.apply_with_context) {
            definition.apply_with_context(context, value);
        } else if (definition.apply_checked) {
            apply_result = definition.apply_checked(&style, value);
        } else if (definition.apply) {
            definition.apply(&style, value);
        }
        if (!apply_result.success || apply_result.diagnostic_code) {
            if (emit_diagnostics) {
                report(runtime,
                       apply_result.diagnostic_code ? apply_result.diagnostic_code : "AETHER-CSS-APPLY",
                       definition.name.c_str(), value.c_str(),
                       apply_result.diagnostic_message ? apply_result.diagnostic_message : "CSS value could not be applied.");
            }
            if (!apply_result.success) return false;
        }
        return true;
    };

    color_resolution_state_s color_references;
    if (parent) {
        // text-shadow is inherited, and currentColor inside an inherited
        // shadow must remain symbolic until this node's own color is known.
        // Background, borders and box-shadow are not inherited, so their symbolic state
        // starts from the CSS initial values for this node.
        color_references.text_shadow_uses_current_color =
            copy_text_color_flags(*parent->color_references);
    }
    uint32_t parent_dependencies=ParentDependencies::other;
    for(const auto& definition:registry->definitions)
        if(definition.inherited&&definition.parent_color_reads)parent_dependencies|=definition.parent_color_reads;
    uint32_t declared_impact = STYLE_IMPACT_NONE;
    uint32_t declared_inherited_impact = STYLE_IMPACT_NONE;
    auto apply_definition = [&](size_t i) {
        if (!cascade[i].is_set) return false;
        const auto& definition = registry->definitions[i];
        const auto& compiled = cascade[i].value;
        const std::string initial_value = definition.name == "font-size" ?
            std::to_string(measures->document->initial_font_size) + "px" : definition.initial_value;
        std::string value;
        bool explicit_inherit = false;
        switch (compiled.mode) {
        case compiled_value_mode::value:
            value = compiled.typed_value;
            break;
        case compiled_value_mode::initial:
            value = initial_value;
            break;
        case compiled_value_mode::inherit:
            if (!definition.inherit_explicit && !definition.inherit_context) return false;
            explicit_inherit = parent != nullptr;
            value = initial_value;
            break;
        case compiled_value_mode::unset:
            if (definition.inherited && parent) return false;
            value = initial_value;
            break;
        case compiled_value_mode::deferred: {
            if (!resolve_frozen_computed_value(compiled.deferred_source.c_str(),
                                  definition.name == "backdrop-filter" ? backdrop_scope.get() : scope.get(),
                                  runtime->options.max_variable_depth,
                                  runtime->options.max_variable_expansion_length, value)) {
                report(runtime, "AETHER-CSS-VAR", definition.name.c_str(),
                       compiled.deferred_source.c_str(), "Unable to resolve CSS variable.");
                if (!definition.css_wide || (definition.inherited && parent)) return false;
                // This non-inherited opt-in property keeps the invalid winner.
                // Applying initial also accounts for its delta from old state.
                value = initial_value;
                break;
            }
            const auto css_wide = definition.css_wide ? definition.css_wide(value) : property_css_wide_e::none;
            if (css_wide != property_css_wide_e::none) {
                if (css_wide == property_css_wide_e::unset && definition.inherited && parent) return false;
                explicit_inherit = css_wide == property_css_wide_e::inherit &&
                    parent && (definition.inherit_explicit || definition.inherit_context);
                value = initial_value;
                break;
            }
            if (!definition.parse(value.c_str(), value)) {
                const auto failure = definition.parse_failure ? definition.parse_failure(value.c_str()) : property_apply_result_s{};
                report(runtime,
                       failure.diagnostic_code ? failure.diagnostic_code :
                           is_calc_expression(value.c_str()) ? "AETHER-CSS-CALC" : "AETHER-CSS-VALUE",
                       definition.name.c_str(), value.c_str(),
                       failure.diagnostic_message ? failure.diagnostic_message :
                           is_calc_expression(value.c_str())
                               ? "The calc() expression is syntactically or dimensionally invalid."
                               : "Resolved CSS value is invalid.");
                if (!definition.css_wide || (definition.inherited && parent)) return false;
                value = initial_value;
            }
            break;
        }
        }
        auto apply_selected = [&](aether_computed_style_t& style,
                                  color_resolution_state_s& references, bool emit_diagnostics,
                                  measure_apply_state_s& destination) {
            parent_dependencies|=definition.parent_color_reads;
            if (explicit_inherit) {
                if(definition.name=="background-color")parent_dependencies|=ParentDependencies::background_color;
                else if(definition.name=="border-left-color"||definition.name=="border-right-color"||
                    definition.name=="border-top-color"||definition.name=="border-bottom-color")
                    parent_dependencies|=ParentDependencies::border_colors;
                else parent_dependencies|=ParentDependencies::other;
                if (definition.inherit_context) {
                    const ColorMetadataReadView colors(*parent->color_references);
                    const MeasuresReadView parent_measures(*parent->measures);
                    property_apply_context_s context{&style, &references, &destination, &colors};
                    definition.inherit_context(context, parent->style->view(), &parent_measures);
                } else definition.inherit_explicit(&style, parent->style->view());
                return true;
            }
            return apply_property_value(definition, style, references, value, emit_diagnostics, destination);
        };
        measure_apply_state_s selected_measures(*measures, definition.name);
        if (!apply_selected(next, color_references, true, selected_measures)) return false;
        bool property_changed = !previous_style;
        if (previous_style) {
            MutableStyleBuilder probe_builder;
            auto& probe=probe_builder.value;
            if (computed_style_deep_copy(*previous_style, &probe) == AETHER_OK) {
                color_resolution_state_s probe_references = copy_color_state(*(*found)->color_references());
                // Read the candidate AFTER the winning declaration, while the
                // flat and symbolic color references deliberately start at OLD.
                measure_property_delta_s delta;
                measure_apply_state_s probe_measures(*measures, delta, definition.name);
                if (apply_selected(probe, probe_references, false, probe_measures)) {
                    resolve_symbolic_colors(probe, probe_references);
                    normalize_overflow_axes(probe);
                    property_changed = !style_equal(*previous_style, probe);
                } else {
                    property_changed = true;
                }

            } else {
                throw std::bad_alloc();
            }

        }
        if (property_changed) {
            declared_impact |= definition.impact;
            if ((definition.impact & STYLE_IMPACT_INHERITED) != 0)
                declared_inherited_impact |= definition.impact;
        }
        return true;
    };

    // Font-size has a semantic phase independent of registry/declaration order.
    for (size_t i = 0; i < cascade.size(); ++i)
        if (registry->definitions[i].name == "font-size") apply_definition(i);
    measures->self_font_size = next.font_size;
    if (document_element) runtime->document_measures->root_font_size = next.font_size;

    // The cascade has already selected winners. Resolve the node's final
    // concrete text color first, then apply properties that may carry symbolic
    // currentColor references. This makes declaration/registry order immaterial.
    for (size_t i = 0; i < cascade.size(); ++i) {
        if (cascade[i].is_set && is_color_producer(registry->definitions[i]))
            apply_definition(i);
    }
    for (size_t i = 0; i < cascade.size(); ++i) {
        if (cascade[i].is_set && !is_color_producer(registry->definitions[i]) && registry->definitions[i].name != "font-size")
            apply_definition(i);
    }
    if (!std::isfinite(measure_used_line_height(measures.get(), next)))
        throw std::overflow_error("Non-finite inherited line-height");
    measure_compute_paint_lengths(next, *measures);
    resolve_symbolic_colors(next, color_references);
    normalize_overflow_axes(next);

    // <br> has an HTML display semantic, not merely a managed bridge convention.
    // Preserve an author-specified display value, but make an otherwise unstyled
    // line break participate in native inline layout as a line-break item.
    if (node->tag_name == "br") {
        const auto* display = registry->try_get("display");
        if (display && !cascade[static_cast<size_t>(display->id)].is_set) {
            next.display = AETHER_DISPLAY_LINE_BREAK;
            declared_impact |= display->impact;
        }
    }

    measure_recompute_dependencies(*measures);
    const bool is_new_node = !found;
    const auto* old_measures = is_new_node ? nullptr : (*found)->measures().get();
    const bool contextual_geometry_changed = old_measures &&
        (std::memcmp(&old_measures->scalars().row_gap, &measures->row_gap, sizeof(aether_css_length)) != 0 ||
         std::memcmp(&old_measures->scalars().column_gap, &measures->column_gap, sizeof(aether_css_length)) != 0 ||
         static_cast<int>(old_measures->scalars().line_height_kind) != static_cast<int>(measures->line_height.kind) ||
         old_measures->scalars().line_height_value != measures->line_height.value);
    if (contextual_geometry_changed) declared_impact |= STYLE_IMPACT_LAYOUT | STYLE_IMPACT_TEXT_LAYOUT;

    const uint32_t inherited_symbolic_impact = !is_new_node &&
        !text_color_flags_equal(*(*found)->color_references(), color_references.text_shadow_uses_current_color)
        ? STYLE_IMPACT_PAINT | STYLE_IMPACT_INHERITED : STYLE_IMPACT_NONE;
    const bool changed = is_new_node || contextual_geometry_changed || !style_equal((*found)->style_view(), next);
    if (!changed) {
        impact = STYLE_IMPACT_NONE;
        inherited_change_impact = inherited_symbolic_impact;
        declared_impact=(*found)->declared_impact();
        declared_inherited_impact=(*found)->declared_inherited_impact();
    } else if (is_new_node) {
        impact = STYLE_IMPACT_ALL_BITS;
        inherited_change_impact = declared_inherited_impact | inherited_parent_impact;
    } else if (!transform_origin_equal((*found)->style_view().scalars().transform_origin, next.transform_origin) &&
               [&] {
                   // Removal or computed-invalid origin can occur after any
                   // number of no-op resolves. Compare the final scalar delta,
                   // not historical declaration impacts. This copy only borrows.
                   auto comparison = (*found)->style_view().scalars();
                   comparison.transform_origin = next.transform_origin;
                   return style_equal((*found)->style_view().with_scalars(comparison), next);
               }()) {
        const auto* origin = registry->try_get("transform-origin");
        impact = origin ? origin->impact : STYLE_IMPACT_ALL_BITS;
        inherited_change_impact = STYLE_IMPACT_NONE;
    } else if ((*found)->style_view().scalars().visible != next.visible) {
        // Compare the final computed delta, not the declarations still present.
        // Removing visibility can otherwise reuse historical geometric impacts;
        // removing width alongside visibility can miss those same impacts.
        // Copy only scalar values. Payload stays readonly under the old record pin.
        auto comparison = (*found)->style_view().scalars();
        comparison.visible = next.visible;
        const auto* visibility = registry->try_get("visibility");
        impact = visibility && style_equal((*found)->style_view().with_scalars(comparison), next)
            ? visibility->impact
            : STYLE_IMPACT_ALL_BITS;
        inherited_change_impact = impact;
    } else {
        impact = declared_impact | inherited_parent_impact;
        if (impact == STYLE_IMPACT_NONE) {
            impact = (*found)->declared_impact();
        }
        if (impact == STYLE_IMPACT_NONE) {
            // Safety net for computed-style changes produced outside a concrete
            // property applicator. This should be rare; known CSS properties
            // should flow through their registry impact instead of this path.
            impact = STYLE_IMPACT_ALL_BITS;
        }
        inherited_change_impact = declared_inherited_impact | inherited_parent_impact;
        if (inherited_change_impact == STYLE_IMPACT_NONE)
            inherited_change_impact = (*found)->declared_inherited_impact();
    }
    inherited_change_impact |= inherited_symbolic_impact;
    if (!is_new_node) {
        const auto previous = (*found)->style_view();
        const bool before_context = !previous.transforms().empty() || !previous.backdrop_filters().empty();
        const bool after_context = next.transforms.count > 0 || next.backdrop_filters.count > 0;
        if (before_context != after_context) impact |= STYLE_IMPACT_LAYOUT;
    }
    auto state = std::make_unique<PreparedNodeResult>();
    if(!changed && complete_background_payload_equal((*found)->style_view(),next)) state->style=(*found)->style();
    else require_style_status(freeze_style(next,state->style));
    state->measures = measures;
    state->custom_properties = std::move(scope);
    state->backdrop_custom_properties = std::move(backdrop_scope);
    // Always refresh symbolic metadata, even when identical RGBA reused the style.
    // prepare_tree visits descendants regardless of parent impact: explicit background
    // inherit must observe concrete <-> currentColor changes with the same parent RGBA.
    require_style_status(freeze_color_metadata(color_references,state->color_references));
    state->declared_impact = declared_impact;
    state->declared_inherited_impact = declared_inherited_impact;
    state->inherited_change_impact = inherited_change_impact;
    auto* result = state.get();
    state->impact=impact;state->parent_dependencies=parent_dependencies;
    runtime->prepared[node] = std::move(state);
    return result;
}

struct prepared_children_s {
    aether_node_s* host = nullptr;
    std::vector<aether_node_s*> children;
    std::vector<std::shared_ptr<aether_node_s>> storage;
};

static std::shared_ptr<aether_node_s> create_generated_node() {
    return std::shared_ptr<aether_node_s>(new aether_node_s(), [](aether_node_s* node) noexcept {
        for (auto* child : node->children) delete child;
        delete node;
    });
}
static std::shared_ptr<aether_node_s> prepare_generated_pseudo(
    aether_style_runtime_s* staged, aether_node_s* host, PreparedNodeResult* host_state,
    aether_pseudo_element_t pseudo) {
    if (!staged->compiled->index.may_target_pseudo(pseudo)) return {};
    auto generated = create_generated_node();
    generated->type = AETHER_NODE_ELEMENT; generated->parent = host;
    generated->tag_name = host->tag_name; generated->attributes = host->attributes;
    generated->state = host->state; generated->is_generated = true;
    generated->snapshot_owned = true; generated->identity->attached = false;
    generated->pseudo_element = pseudo; generated->style_origin = host;
    uint32_t impact = 0, inherited = 0;
    std::optional<std::string> content;
    compute_node(staged, generated.get(), host_state,
        host_state ? host_state->inherited_change_impact : 0, impact, inherited, &content);
    if (!content) { staged->prepared.erase(generated.get()); return {}; }
    if (!content->empty()) {
        auto text = std::make_unique<aether_node_s>(); text->type = AETHER_NODE_TEXT;
        text->parent = generated.get(); text->text = std::move(*content);
        generated->children.push_back(text.get()); text.release();
    }
    return generated;
}
static void prepare_tree(aether_style_runtime_s* staged, aether_node_s* node,
    PreparedNodeResult* parent, uint32_t inherited_parent,
    std::vector<prepared_children_s>& links, std::vector<aether_node_s*>& order) {
    if (!node || node->is_generated) return;
    auto* next_parent = parent;
    uint32_t next_inherited = inherited_parent;
    if (node->type == AETHER_NODE_ELEMENT) {
        uint32_t impact = 0;
        next_parent = compute_node(staged, node, parent, inherited_parent, impact, next_inherited);
        order.push_back(node);
        prepared_children_s link; link.host = node;
        link.children.reserve(node->children.size() + 2); link.storage.reserve(2);
        auto before = prepare_generated_pseudo(staged, node, next_parent, aether_pseudo_element_t::before);
        auto after = prepare_generated_pseudo(staged, node, next_parent, aether_pseudo_element_t::after);
        if (before) { link.children.push_back(before.get()); order.push_back(before.get()); link.storage.push_back(std::move(before)); }
        for (auto* child : node->children) if (!child->is_generated) link.children.push_back(child);
        if (after) { link.children.push_back(after.get()); order.push_back(after.get()); link.storage.push_back(std::move(after)); }
        links.push_back(std::move(link));
    }
    for (auto* child : node->children)
        if (!child->is_generated) prepare_tree(staged, child, next_parent, next_inherited, links, order);
}

AETHER_API aether_status_t aether_style_runtime_create(
    const aether_style_runtime_options_t* options,
    const aether_style_environment_t* environment,
    aether_style_diagnostic_fn diagnostics_callback,
    void* diagnostics_user_data,
    aether_style_runtime_t* out_runtime) {
    if (!out_runtime) return AETHER_ERROR_NULL_ARG;
    *out_runtime = nullptr;
    try {
    style_property_registry_init();
    auto* runtime = new (std::nothrow) aether_style_runtime_s();
    if (!runtime) return AETHER_ERROR_OUT_OF_MEMORY;
    runtime->registry=style_property_registry_pin();
    runtime->options = options ? *options : aether_style_runtime_options_t{};
    if (runtime->options.max_variable_depth <= 0) runtime->options.max_variable_depth = 32;
    if (runtime->options.max_variable_expansion_length <= 0)
        runtime->options.max_variable_expansion_length = 65536;
    runtime->environment = environment ? *environment :
        aether_style_environment_t{1280, 720, 1, 16, 0, 0, 0};
    runtime->diag_cb = diagnostics_callback;
    runtime->diag_user_data = diagnostics_user_data;
    *out_runtime = runtime;
    return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
}

AETHER_API void aether_style_runtime_set_diagnostics_callback(
    aether_style_runtime_t runtime, aether_style_diagnostic_fn callback, void* user_data) {
    if (runtime) { runtime->diag_cb = callback; runtime->diag_user_data = user_data; }
}

AETHER_API void aether_style_runtime_destroy(aether_style_runtime_t runtime) { delete runtime; }

AETHER_API aether_status_t aether_style_runtime_add_stylesheet(
    aether_style_runtime_t runtime, const char* source, int32_t layer, const char* source_name,
    int64_t* out_handle, int32_t* out_rule_count) {
    if (!runtime || !source) return AETHER_ERROR_NULL_ARG;
    if (runtime->preparing || runtime->owner_thread != std::this_thread::get_id()) return AETHER_ERROR_INVALID_STATE;
    try {
    stylesheet_diagnostics_s diagnostics(runtime->diag_cb, runtime->diag_user_data);
    const auto collector = diagnostics.collector();
    const auto status = [&]() -> aether_status_t {
        if (!style_sheet_handle_available(runtime->next_sheet_handle)) {
            if (collector) collector("AETHER-CSS-LIMIT", "", "",
                "The cumulative stylesheet handle capacity is exhausted.", &diagnostics);
            return AETHER_ERROR_INVALID_STATE;
        }
        const auto handle = runtime->next_sheet_handle;
        auto next_rule_id = runtime->next_rule_id;
        auto sheet = std::make_shared<compiled_style_sheet_s>();
        sheet->handle = handle;
        sheet->layer = static_cast<style_sheet_layer>(layer);
        sheet->source_name = source_name ? source_name : "";
        const auto result = style_source_compile(source, handle, layer, sheet->rules, next_rule_id,
                                                collector, &diagnostics);
        if (result != AETHER_OK) return result;
        // Preserve the compiler's global positions, including the shared position
        // of selectors in one list. ID capacity already bounds this narrowing.
        const auto rule_count = static_cast<int32_t>(sheet->rules.size());
        auto index = aether_style_runtime_s::prepare_rule_index(runtime, sheet);
        runtime->sheets.emplace(handle, std::move(sheet)); // Last fallible allocation.
        using std::swap;
        swap(runtime->compiled, index);
        runtime->states.clear();
        ++runtime->sheet_epoch;
        runtime->next_sheet_handle = handle + 1;
        runtime->next_rule_id = next_rule_id;
        if (out_handle) *out_handle = handle;
        if (out_rule_count) *out_rule_count = rule_count;
        return AETHER_OK;
    }(); // Failed staging is destroyed before any user callback can run.
    try { diagnostics.replay(); } catch (...) { /* Callback exceptions do not roll back committed input. */ }
    // Callbacks may have changed sheets, counters, or resolved state. Never read
    // or write runtime here, and never replay from a destructor during unwind.
    return status;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
}

AETHER_API aether_bool_t aether_style_runtime_remove_stylesheet(
    aether_style_runtime_t runtime, int64_t sheet_handle) {
    if (!runtime || runtime->preparing || runtime->owner_thread != std::this_thread::get_id() ||
        !runtime->sheets.count(sheet_handle)) return AETHER_FALSE;
    try {
        auto index = aether_style_runtime_s::prepare_rule_index(runtime, {}, sheet_handle);
        using std::swap;
        swap(runtime->compiled, index);
        runtime->sheets.erase(sheet_handle);
        runtime->states.clear(); ++runtime->sheet_epoch;
        return AETHER_TRUE;
    } catch (...) { return AETHER_FALSE; }
}

static int32_t resolve_legacy_atomic(aether_style_runtime_t runtime, aether_node_t root,
    const aether_style_environment_t* environment, aether_style_update_t** out_updates, bool isolate_legacy_root = false);
AETHER_API int32_t aether_style_runtime_resolve_subtree(
    aether_style_runtime_t runtime, aether_node_t root, aether_style_update_t** out_updates) {
    return resolve_legacy_atomic(runtime, root, nullptr, out_updates, true);
}
AETHER_API int32_t aether_style_runtime_resolve_state_changes(
    aether_style_runtime_t runtime, aether_node_t root, const aether_node_t* changed_nodes,
    int32_t changed_count, aether_style_update_t** out_updates) {
    if (out_updates) *out_updates = nullptr;
    if (!changed_nodes || changed_count <= 0) return 0;
    if (runtime && root && out_updates) count_work(WorkCounter::state_full_fallbacks);
    // Conservatively visit the complete tree: font/viewport and metadata edges
    // cannot be inferred from a visual-only changed-node selection.
    return resolve_legacy_atomic(runtime, root, nullptr, out_updates);
}

AETHER_API aether_status_t aether_style_runtime_get_update(
    aether_style_update_t update, aether_node_t* out_node,
    aether_computed_style_handle_t* out_current, uint32_t* out_impact) {
    if (out_current) *out_current = nullptr;
    if (out_node) *out_node = nullptr;
    if (out_impact) *out_impact = 0;
    if (!update) return AETHER_ERROR_NULL_ARG;
    if (out_current) {
        auto* copy = new (std::nothrow) aether_computed_style_handle_s();
        if (!copy) return AETHER_ERROR_OUT_OF_MEMORY;
        if (aether_computed_style_copy(&update->current, &copy->style) != AETHER_OK) {
            delete copy;
            return AETHER_ERROR_OUT_OF_MEMORY;
        }
        copy->record=update->current.record;
        copy->measures = update->current.measures; copy->revision = update->current.revision;
        copy->document_identity = update->current.document_identity; copy->node_identity = update->current.node_identity;
        copy->node_lifetime = update->current.node_lifetime; copy->document_lifetime = update->current.document_lifetime;
        *out_current = copy;
    }
    if (out_node) *out_node = update->node;
    if (out_impact) *out_impact = update->impact;
    return AETHER_OK;
}

AETHER_API void aether_style_updates_free(aether_style_update_t* updates, int32_t count) {
    if (!updates) return;
    for (int32_t i = 0; i < count; ++i) delete updates[i];
    delete[] updates;
}

AETHER_API int32_t aether_style_runtime_set_environment(
    aether_style_runtime_t runtime, const aether_style_environment_t* environment,
    aether_node_t root, aether_style_update_t** out_updates) {
    if (!environment) { if (out_updates) *out_updates = nullptr; return 0; }
    return resolve_legacy_atomic(runtime, root, environment, out_updates);
}

struct aether_style_transaction_s {
    aether_style_runtime_s* owner = nullptr;
    StyleRevisionRef base_pin;
    std::shared_ptr<const style_property_registry_s> base_registry_pin;
    std::shared_ptr<const CompiledStyleInputs> base_compiled_pin;
    uint64_t base_generation = 0, base_sheet_epoch = 0, input_epoch = 0, base_style_tree_epoch = 0;
    std::shared_ptr<node_identity_s> document_lifetime;
    std::unique_ptr<aether_style_runtime_s> candidate;
    std::vector<std::unique_ptr<aether_style_update_s>> updates;
    std::unique_ptr<aether_style_update_t[]> legacy_array;
    std::vector<prepared_children_s> links;
    StyleRevisionRef revision;
    std::unique_ptr<aether_style_revision_handle_s> output;
    std::unique_ptr<stylesheet_diagnostics_s> diagnostics;
    bool committed = false;
    bool state_operation = false;
    aether_style_state_result_t state_result{};
    std::vector<size_t> semantic_delta_positions;
    std::vector<ReconciledState> reconciled_inputs;
    void acknowledge_full_publication() const noexcept {
        if(state_result.incremental||!committed||!revision||!revision->contextual()||!document_lifetime||!document_lifetime->alive||
            document_lifetime->mutation_epoch!=input_epoch)return;
        auto* node=static_cast<aether_node_s*>(const_cast<void*>(document_lifetime->owner));
        if(auto* document=dynamic_cast<aether_doc_s*>(node))
            document->mutation_journal.acknowledge_full_publication(input_epoch);
    }
};

static bool valid_measure_environment(const aether_style_environment_t& env) noexcept {
    return std::isfinite(env.viewport_width) && env.viewport_width > 0 &&
        std::isfinite(env.viewport_height) && env.viewport_height > 0 &&
        std::isfinite(env.root_font_size) && env.root_font_size >= 0 &&
        std::isfinite(env.dpi_scale) && env.dpi_scale > 0;
}
AETHER_API uint32_t aether_contextual_style_version(void) { return 1; }
static aether_status_t prepare_revision_impl(
    aether_style_runtime_t runtime, aether_node_t root,
    const aether_style_environment_t* environment, aether_style_transaction_t* out_transaction, bool isolate_legacy_root, bool export_legacy_updates) {
    if (out_transaction) *out_transaction = nullptr;
    if (!runtime || !root || !out_transaction) return AETHER_ERROR_NULL_ARG;
    if (runtime->preparing || runtime->owner_thread != std::this_thread::get_id()) return AETHER_ERROR_INVALID_STATE;
    count_work(WorkCounter::revision_prepares);
    const auto requested_environment = environment ? *environment : runtime->environment;
    if (!valid_measure_environment(requested_environment)) return AETHER_ERROR_INVALID_STATE;
    struct guard_s { bool& active; explicit guard_s(bool& v) : active(v) { active = true; } ~guard_s() { active = false; } } guard(runtime->preparing);
    try {
        auto transaction = std::make_unique<aether_style_transaction_s>();
        transaction->owner = runtime; transaction->base_pin = runtime->revision; transaction->base_registry_pin=runtime->registry; transaction->base_compiled_pin=runtime->revision_compiled; transaction->base_generation = runtime->generation;
        transaction->base_sheet_epoch = runtime->sheet_epoch;
        auto* document = aether_dom_root(root);
        transaction->document_lifetime = document->identity;
        transaction->input_epoch = document->identity->mutation_epoch;
        transaction->base_style_tree_epoch = document->identity->style_tree_epoch;
        transaction->candidate = std::make_unique<aether_style_runtime_s>();
        auto& staged = *transaction->candidate;
        staged.options = runtime->options; staged.environment = requested_environment;
        staged.registry=style_property_registry_pin();if(!staged.registry||!staged.registry->sealed)return AETHER_ERROR_INVALID_STATE;
        if (isolate_legacy_root && root != document && root->parent && root->parent->type != AETHER_NODE_DOCUMENT)
            staged.legacy_inheritance_root = root;
        staged.compiled = runtime->compiled;
        staged.document_measures = std::make_shared<document_measure_context_s>();
        staged.document_measures->viewport_width = requested_environment.viewport_width;
        staged.document_measures->viewport_height = requested_environment.viewport_height;
        staged.document_measures->initial_font_size = requested_environment.root_font_size;
        staged.document_measures->root_font_size = requested_environment.root_font_size;
        staged.document_measures->generation = runtime->generation + 1;
        staged.document_measures->identity = aether_node_identity(document);
        transaction->diagnostics = std::make_unique<stylesheet_diagnostics_s>(runtime->diag_cb, runtime->diag_user_data);
        staged.diag_cb = transaction->diagnostics->collector(); staged.diag_user_data = transaction->diagnostics.get();
        // Lookup checks retained token liveness before reusing a raw owner.
        // Sharing this owner is constant work; compute_node counts actual reads.
        staged.states = runtime->states;
        std::vector<aether_node_s*> order;
        prepare_tree(&staged, document, nullptr, 0, transaction->links, order);
        std::unordered_set<aether_node_s*> current_nodes(order.begin(), order.end());
        // Freeze document once, only after root-font bootstrap and complete traversal.
        FullPayloadRevisionInput revision_input;
        revision_input.contextual=staged.legacy_inheritance_root==nullptr;
        const auto& d=*staged.document_measures;
        require_style_status(freeze_document({d.viewport_width,d.viewport_height,d.initial_font_size,d.root_font_size,d.generation,d.identity},revision_input.document));
        revision_input.generation=d.generation; revision_input.document_identity=d.identity;
        revision_input.input_epoch=transaction->input_epoch;
        revision_input.structure_epoch=document->identity->structure_epoch;
        revision_input.style_tree_epoch=document->identity->style_tree_epoch+1;
        revision_input.document_lifetime=document->identity; revision_input.environment=requested_environment;
        std::unordered_map<aether_node_s*,std::shared_ptr<aether_node_s>> leaves;
        for(const auto& link:transaction->links)for(const auto& leaf:link.storage)leaves.emplace(leaf.get(),leaf);
        revision_input.entries.reserve(order.size());
        for(auto* node:order) {
            const auto& prepared=*staged.prepared.at(node);
            NodePayloadInput input;
            input.parent_dependencies=prepared.parent_dependencies;
            input.origin=RecordOrigin::runtime; input.node=node; input.style=prepared.style;
            require_style_status(freeze_runtime_measure_payload(*prepared.measures,input.measures));
            input.custom_properties=prepared.custom_properties;input.color_references=prepared.color_references;
            input.backdrop_custom_properties=prepared.backdrop_custom_properties;
            input.declared_impact=prepared.declared_impact;input.declared_inherited_impact=prepared.declared_inherited_impact;
            // Propagation for this operation already completed in prepare_tree.
            // Do not persist its transient output as a future semantic input.
            // PreparedNodeResult and private import factories retain real values.
            input.inherited_change_impact=0;
            input.document_identity=d.identity;input.node_identity=aether_node_identity(node);
            input.node_lifetime=node->identity;input.document_lifetime=document->identity;
            if(auto it=leaves.find(node);it!=leaves.end())input.generated_leaf=it->second;
            FrozenNodePayloadRef payload;require_style_status(freeze_node_payload(input,payload));
            revision_input.entries.push_back({node,payload,prepared.impact});
            count_work(WorkCounter::revision_entries_created);
        }
        // The full resolver supplies every current output; certification never
        // infers unchanged styles from DOM epochs or a caller-supplied flag.
        CertifiedRevisionDeltaRef certificate;
        const auto base=runtime->revision && runtime->revision->document_identity()==d.identity ? runtime->revision : StyleRevisionRef{};
        if(!base&&runtime->revision)for(const auto& old:runtime->revision->entries())
            if(!current_nodes.count(old.node()))revision_input.retired.push_back({old.node(),old.payload(),STYLE_IMPACT_ALL_BITS});
        require_style_status(certify_full_payload_revision(base,revision_input,certificate));
        StyleRevisionRef revision;require_style_status(freeze_certified_revision(certificate,revision));
        for(const auto& entry:revision_input.entries)if(export_legacy_updates && entry.impact) {
            auto update=std::make_unique<aether_style_update_s>();update->node=entry.node;update->impact=entry.impact;
            FrozenNodeRef record;require_style_status(BoundNodeView::find(revision,entry.node).export_record(record));
            require_style_status(computed_style_deep_copy(record->style_view(),&update->current.style));
            update->current.record=record;update->current.measures=record->measures();update->current.revision=revision;
            update->current.document_identity=record->document_identity();update->current.node_identity=record->node_identity();
            update->current.node_lifetime=record->node_lifetime();update->current.document_lifetime=record->document_lifetime();
            transaction->updates.push_back(std::move(update));
        }
        staged.states=RuntimeStateCache(revision);staged.prepared.clear();
        if(transaction->updates.size()>static_cast<size_t>(INT32_MAX))return AETHER_ERROR_INVALID_STATE;
        // Reserve the last outward allocation before publishing any state.
        if (!transaction->updates.empty()) {
            transaction->legacy_array = std::make_unique<aether_style_update_t[]>(transaction->updates.size());
            for (size_t i = 0; i < transaction->updates.size(); ++i) transaction->legacy_array[i] = transaction->updates[i].get();
        }
        transaction->revision = revision;
        transaction->output = std::make_unique<aether_style_revision_handle_s>(); transaction->output->value = revision;
        if (!document->identity->alive || document->identity->mutation_epoch != transaction->input_epoch ||
            runtime->generation != transaction->base_generation || runtime->sheet_epoch != transaction->base_sheet_epoch ||
            document->identity->style_tree_epoch != transaction->base_style_tree_epoch||staged.registry!=style_property_registry_pin())
            return AETHER_ERROR_INVALID_STATE;
        *out_transaction = transaction.release();
        return AETHER_OK;
    } catch (const std::bad_alloc&) { return AETHER_ERROR_OUT_OF_MEMORY; }
      catch (...) { return AETHER_ERROR_INVALID_STATE; }
}
AETHER_API aether_status_t aether_style_runtime_prepare_revision(
    aether_style_runtime_t runtime, aether_node_t root,
    const aether_style_environment_t* environment, aether_style_transaction_t* out_transaction) {
    return prepare_revision_impl(runtime, root, environment, out_transaction, false, false);
}
static std::unique_ptr<PreparedNodeResult> bind_parent_for_preparation(
    const FrozenNodePayload& source,const std::shared_ptr<document_measure_context_s>& document) {
    auto result=std::make_unique<PreparedNodeResult>();result->style=source.style();
    result->custom_properties=source.custom_properties();result->backdrop_custom_properties=source.backdrop_custom_properties();
    result->color_references=source.color_references();result->parent_dependencies=source.parent_dependencies();
    result->measures=std::make_shared<measure_state_s>();auto& values=*result->measures;values.document=document;
    const auto& frozen=*source.measures();const auto& scalar=frozen.scalars();
    values.parent_font_size=scalar.parent_font_size;values.self_font_size=scalar.self_font_size;
    values.line_height={static_cast<measure_line_height_kind_e>(scalar.line_height_kind),scalar.line_height_value};
    values.row_gap=scalar.row_gap;values.column_gap=scalar.column_gap;values.dependencies=scalar.dependencies;
    values.background_positions.clear();values.background_sizes.clear();
    for(const auto& position:frozen.background_positions())values.background_positions.push_back({position.x,position.y});
    for(const auto& size:frozen.background_sizes())values.background_sizes.push_back({size.kind,size.x,size.y});
    for(const auto& dependency:frozen.property_dependencies())
        values.property_dependencies.emplace(std::string(dependency.name().c_str(),dependency.name().size()),dependency.flags());
    return result;
}
static FrozenNodePayloadRef freeze_prepared_state(aether_node_s* node,const PreparedNodeResult& prepared,
    const std::shared_ptr<node_identity_s>& document) {
    NodePayloadInput input;input.origin=RecordOrigin::runtime;input.node=node;input.style=prepared.style;
    input.parent_dependencies=prepared.parent_dependencies;
    require_style_status(freeze_runtime_measure_payload(*prepared.measures,input.measures));
    input.custom_properties=prepared.custom_properties;input.backdrop_custom_properties=prepared.backdrop_custom_properties;
    input.color_references=prepared.color_references;input.declared_impact=prepared.declared_impact;
    input.declared_inherited_impact=prepared.declared_inherited_impact;
    // Match the canonical FULL payload; current Prepared propagation stays real.
    input.inherited_change_impact=0;
    input.document_lifetime=document;input.document_identity=reinterpret_cast<uintptr_t>(document.get());
    input.node_lifetime=node->identity;input.node_identity=aether_node_identity(node);
    FrozenNodePayloadRef result;require_style_status(freeze_node_payload(input,result));return result;
}
// This is the only incremental certificate producer. It has no public factory
// taking an unchecked list of unchanged leaves: it performs journal/AST proof
// within preparation and owns every input until the transaction is retired.
class aether::style::StateRevisionPreparation {
public:
    static aether_status_t prepare_local(aether_style_runtime_s* runtime,aether_node_s* document,
        aether_style_transaction_t* output,
        aether_state_fallback_t& fallback) {
        *output=nullptr;
        const auto base=runtime->revision;
        if(!base||!base->authentic_owner(base)||!base->contextual()||base->document_lifetime()!=document->identity||
            base->backing()||runtime->revision_compiled!=runtime->compiled||
            runtime->revision_registry!=style_property_registry_pin())
            return AETHER_ERROR_INVALID_STATE;
        if(runtime->generation!=base->generation()||runtime->sheet_epoch!=runtime->revision_sheet_epoch||
            document->identity->structure_epoch!=base->structure_epoch()||
            document->identity->style_tree_epoch!=base->style_tree_epoch())return AETHER_ERROR_INVALID_STATE;
        const auto* journal=aether_node_mutation_journal(document);
        if(!journal)return AETHER_ERROR_INVALID_STATE;
        std::vector<ReconciledState> reconciled;
        const auto reason=reconcile_state_inputs(base,*journal,document->identity->mutation_epoch,nullptr,0,reconciled);
        if(reason!=AETHER_STATE_FALLBACK_NONE){fallback=reason;return AETHER_OK;}
        std::vector<invalidation::StateChange> changes;
        for(const auto& item:reconciled) {
            if(item.node->is_generated||item.node->anonymous_flex_owner){fallback=AETHER_STATE_FALLBACK_PSEUDO;return AETHER_OK;}
            const auto bits=static_cast<uint32_t>(item.old_state)^static_cast<uint32_t>(item.new_state);
            if(bits)changes.push_back({item.node,bits});
        }
        // The reference planner's result is a superset independent of current
        // matches. Empty means no selector subject can change, not zero pixels.
        // Relational work is still unindexed and therefore not claimed as a
        // fast category in this checkpoint; reject it before any traversal.
        for(const auto& program:runtime->compiled->state_programs)
            for(const auto& occurrence:program.occurrences)
                for(const auto& change:changes)if((occurrence.state_mask&change.changed_bits)&&
                    invalidation::source_applies(occurrence.source_filter,change.node)) {
                    if(occurrence.coverage!=invalidation::Coverage::proven_superset||
                        std::any_of(occurrence.route.begin(),occurrence.route.end(),[](const auto& hop){return hop.walk!=invalidation::Walk::self;})) {
                        fallback=AETHER_STATE_FALLBACK_SELECTOR;return AETHER_OK;
                    }
                }
        const auto candidates=invalidation::plan(document,runtime->compiled->state_programs,changes);
        if(!candidates.empty()&&(runtime->compiled->index.may_target_pseudo(aether_pseudo_element_t::before)||
            runtime->compiled->index.may_target_pseudo(aether_pseudo_element_t::after))) {
            fallback=AETHER_STATE_FALLBACK_PSEUDO;return AETHER_OK;
        }
        if(runtime->generation==UINT64_MAX||document->identity->style_tree_epoch==UINT64_MAX)
            return AETHER_ERROR_INVALID_STATE;
        auto transaction=std::make_unique<aether_style_transaction_s>();
        transaction->owner=runtime;transaction->base_pin=base;transaction->base_registry_pin=runtime->registry;transaction->base_compiled_pin=runtime->revision_compiled;
        transaction->base_generation=runtime->generation;transaction->base_sheet_epoch=runtime->sheet_epoch;
        transaction->document_lifetime=document->identity;transaction->input_epoch=document->identity->mutation_epoch;
        transaction->base_style_tree_epoch=document->identity->style_tree_epoch;
        transaction->candidate=std::make_unique<aether_style_runtime_s>();
        auto& staged=*transaction->candidate;staged.options=runtime->options;staged.environment=runtime->environment;
        staged.compiled=runtime->compiled;staged.registry=runtime->revision_registry;
        staged.document_measures=std::make_shared<document_measure_context_s>(*runtime->document_measures);
        staged.document_measures->generation=runtime->generation+1;
        transaction->diagnostics=std::make_unique<stylesheet_diagnostics_s>(runtime->diag_cb,runtime->diag_user_data);
        staged.diag_cb=transaction->diagnostics->collector();staged.diag_user_data=transaction->diagnostics.get();
        staged.states=runtime->states;
        // Every agenda/parent/child read comes from the authenticated base. Pin
        // ownership, not just the token address, before dereferencing its owner.
        auto live_entry=[&](aether_node_s* node) {
            const auto* entry=base->directory().find(reinterpret_cast<uintptr_t>(node));
            if(!entry)throw std::invalid_argument("missing agenda identity");
            const auto& token=entry->payload->node_lifetime();
            if(!token||!token->alive||!token->attached||token->owner!=node||
                entry->payload->node()!=node||entry->payload->document_lifetime()!=document->identity||
                entry->payload->node_identity()!=reinterpret_cast<uintptr_t>(token.get()))
                throw std::invalid_argument("dead agenda identity");
            if(token!=node->identity||token.owner_before(node->identity)||node->identity.owner_before(token))
                throw std::invalid_argument("unproven agenda identity owner");
            return entry;
        };
        std::map<size_t,aether_node_s*> agenda;
        auto enqueue=[&](aether_node_s* node) {
            const auto* entry=live_entry(node);
            agenda.emplace(entry->position,node);
        };
        for(const auto& candidate:candidates) {
            if(candidate.slot!=aether_pseudo_element_t::none){fallback=AETHER_STATE_FALLBACK_PSEUDO;return AETHER_OK;}
            enqueue(candidate.host);
        }
        std::unordered_map<aether_node_s*,std::unique_ptr<PreparedNodeResult>> parents;
        std::vector<CertifiedRevisionDelta::Change> outputs;
        std::vector<std::pair<uintptr_t,uint32_t>> impacts;
        while(!agenda.empty()) {
            auto it=agenda.begin();const auto position=it->first;auto* node=it->second;agenda.erase(it);
            PreparedNodeResult* parent=nullptr;
            auto* parent_node=node->parent;
            while(parent_node&&parent_node->type!=AETHER_NODE_ELEMENT)parent_node=parent_node->parent;
            if(parent_node) {
                if(const auto found=staged.prepared.find(parent_node);found!=staged.prepared.end())parent=found->second.get();
                else {
                    auto cached=parents.find(parent_node);
                    if(cached==parents.end()) {
                        const auto* source=live_entry(parent_node);
                        count_work(WorkCounter::state_entries_visited);
                        cached=parents.emplace(parent_node,bind_parent_for_preparation(*source->payload,staged.document_measures)).first;
                    }
                    parent=cached->second.get();
                }
            }
            uint32_t impact=0,inherited=0;
            const auto* computed=compute_node(&staged,node,parent,parent?parent->inherited_change_impact:0,impact,inherited);
            if(staged.document_measures->root_font_size!=base->document()->values().root_font_size) {
                fallback=AETHER_STATE_FALLBACK_ROOT_FONT;return AETHER_OK;
            }
            auto payload=freeze_prepared_state(node,*computed,document->identity);
            const auto* previous=base->directory().find(reinterpret_cast<uintptr_t>(node));
            const auto delta=compare_node_semantics(*previous->payload,*payload);
            if(delta.semantic)outputs.push_back({position,node,std::move(payload),node->state});
            if(impact)impacts.push_back({position,impact});
            if(delta.semantic||impact)transaction->semantic_delta_positions.push_back(position);
            if(impact){++transaction->state_result.visual_delta_count;transaction->state_result.visual_impact|=impact;}
            if(delta.propagation) {
                // The initial category walks direct logical children; every
                // examined payload is counted. It never rebuilds a DOM index.
                std::vector<aether_node_s*> pending(node->children.begin(),node->children.end());
                while(!pending.empty()) {
                    auto* child=pending.back();pending.pop_back();
                    if(child->is_generated||child->anonymous_flex_owner)continue;
                    if(child->type!=AETHER_NODE_ELEMENT) {
                        pending.insert(pending.end(),child->children.begin(),child->children.end());continue;
                    }
                    const auto* current=live_entry(child);
                    count_work(WorkCounter::state_entries_visited);
                    if(delta.propagation&current->payload->parent_dependencies())enqueue(child);
                }
            }
        }
        if(document->identity->mutation_epoch!=transaction->input_epoch||
            document->identity->style_tree_epoch!=transaction->base_style_tree_epoch||
            runtime->revision!=base||runtime->sheet_epoch!=transaction->base_sheet_epoch||
            staged.registry!=style_property_registry_pin())
            return AETHER_ERROR_INVALID_STATE;
        FullPayloadRevisionInput header;
        header.generation=runtime->generation+1;header.document_identity=base->document_identity();
        header.input_epoch=transaction->input_epoch;header.structure_epoch=base->structure_epoch();
        header.style_tree_epoch=transaction->base_style_tree_epoch+1;header.document_lifetime=document->identity;
        header.environment=base->environment();auto values=base->document()->values();values.generation=header.generation;
        require_style_status(freeze_document(values,header.document));
        std::vector<std::pair<uintptr_t,int32_t>> ledger;
        for(const auto& item:reconciled)if(item.old_state!=item.new_state)
            ledger.push_back({reinterpret_cast<uintptr_t>(item.node),item.new_state});
        CertifiedRevisionDeltaRef certificate(new const CertifiedRevisionDelta(base,std::move(header),true,
            std::move(outputs),std::move(ledger),std::move(impacts),{}));
        certificate->self_=certificate;
        require_style_status(freeze_certified_revision(certificate,transaction->revision));
        staged.states=RuntimeStateCache(transaction->revision);staged.prepared.clear();
        transaction->state_result.semantic_delta_count=static_cast<int32_t>(transaction->semantic_delta_positions.size());
        transaction->output=std::make_unique<aether_style_revision_handle_s>();
        transaction->output->value=transaction->revision;
        transaction->state_result.incremental=AETHER_TRUE;
        // Epochs and pinned base are checked again by commit. No callback or DOM
        // mutation occurred here; invalidation proof never outlives its inputs.
        *output=transaction.release();fallback=AETHER_STATE_FALLBACK_NONE;
        count_work(WorkCounter::revision_prepares);return AETHER_OK;
    }
};
static bool equal_environment(const aether_style_environment_t& a,const aether_style_environment_t& b) noexcept {
    return a.viewport_width==b.viewport_width&&a.viewport_height==b.viewport_height&&
        a.dpi_scale==b.dpi_scale&&a.root_font_size==b.root_font_size&&
        a.color_scheme==b.color_scheme&&a.pointer_accuracy==b.pointer_accuracy&&
        a.motion_preference==b.motion_preference;
}
AETHER_API uint32_t aether_state_style_version(void) { return 1; }
AETHER_API aether_status_t aether_style_runtime_prepare_state_revision(
    aether_style_runtime_t runtime,aether_node_t root,aether_style_revision_t base_revision,
    const aether_style_state_change_t* changes,int32_t change_count,
    const aether_style_environment_t* environment,aether_style_transaction_t* out_transaction) {
    if(out_transaction)*out_transaction=nullptr;
    if(!runtime||!root||!out_transaction||(!changes&&change_count>0))return AETHER_ERROR_NULL_ARG;
    if(change_count<0||runtime->preparing||runtime->owner_thread!=std::this_thread::get_id())return AETHER_ERROR_INVALID_STATE;
    try {
        const auto requested=environment?*environment:runtime->environment;
        if(!valid_measure_environment(requested))return AETHER_ERROR_INVALID_STATE;
        const auto base=runtime->revision;
        auto* document=aether_dom_root(root);
        std::vector<ReconciledState> reconciled;
        auto fallback=AETHER_STATE_FALLBACK_DISABLED;
        if(!base)fallback=AETHER_STATE_FALLBACK_FIRST_REVISION;
        else if(!base_revision||base_revision->value!=base||!base->authentic_owner(base_revision->value)||
            !base->contextual()||base->document_lifetime()!=document->identity)fallback=AETHER_STATE_FALLBACK_BASE_REVISION;
        else if(runtime->revision_registry!=style_property_registry_pin())fallback=AETHER_STATE_FALLBACK_REGISTRY;
        else if(runtime->compiled!=runtime->revision_compiled||runtime->sheet_epoch!=runtime->revision_sheet_epoch)fallback=AETHER_STATE_FALLBACK_SHEET;
        else if(!equal_environment(requested,base->environment()))fallback=AETHER_STATE_FALLBACK_ENVIRONMENT;
        else if(base->structure_epoch()!=document->identity->structure_epoch||
            base->style_tree_epoch()!=document->identity->style_tree_epoch)fallback=AETHER_STATE_FALLBACK_STRUCTURE;
        else if(const auto* journal=aether_node_mutation_journal(document)) {
            const auto reason=reconcile_state_inputs(base,*journal,document->identity->mutation_epoch,
                changes,static_cast<size_t>(change_count),reconciled);
            if(reason!=AETHER_STATE_FALLBACK_NONE)fallback=reason;
        } else fallback=AETHER_STATE_FALLBACK_JOURNAL_MISSING;
        // FULL remains the fallback. Only the private gated producer can turn
        // reconciled coverage and a proved local agenda into reuse authority.
        aether_style_transaction_t raw=nullptr;
#if defined(AETHERUI_ENABLE_STATE_INCREMENTAL)
        if(fallback==AETHER_STATE_FALLBACK_DISABLED) {
            struct guard_s {bool& value;explicit guard_s(bool& v):value(v){value=true;}~guard_s(){value=false;}} guard(runtime->preparing);
            const auto status=StateRevisionPreparation::prepare_local(runtime,document,&raw,fallback);
            if(status!=AETHER_OK)return status; // OOM never triggers a larger automatic retry.
        }
#endif
        if(!raw) {
            const auto status=prepare_revision_impl(runtime,root,&requested,&raw,false,false);
            if(status!=AETHER_OK)return status;
        }
        std::unique_ptr<aether_style_transaction_s> transaction(raw);
        transaction->state_operation=true;
        transaction->reconciled_inputs=std::move(reconciled);
        auto& result=transaction->state_result;
        const auto& revision=transaction->revision;
        result.struct_size=sizeof(result);result.fallback=fallback;
        result.generation=revision->generation();result.document_identity=revision->document_identity();
        result.input_epoch=revision->input_epoch();result.structure_epoch=revision->structure_epoch();
        result.style_tree_epoch=revision->style_tree_epoch();
        size_t position=0;
        if(!result.incremental)for(const auto& entry:revision->changes()) {
            const auto* previous=base?base->directory().find(reinterpret_cast<uintptr_t>(entry.node())):nullptr;
            if(!entry.payload()||!previous||previous->payload!=entry.payload()||entry.impact()) {
                transaction->semantic_delta_positions.push_back(position);
                ++result.semantic_delta_count;
                if(entry.impact()){++result.visual_delta_count;result.visual_impact|=entry.impact();}
            }
            ++position;
        }
        *out_transaction=transaction.release();return AETHER_OK;
    } catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}
      catch(...){return AETHER_ERROR_INVALID_STATE;}
}
AETHER_API aether_status_t aether_style_transaction_state_result(
    aether_style_transaction_t transaction,aether_style_state_result_t* out_result) {
    if(!transaction||!out_result)return AETHER_ERROR_NULL_ARG;
    if(!transaction->state_operation||out_result->struct_size!=sizeof(*out_result))return AETHER_ERROR_INVALID_STATE;
    *out_result=transaction->state_result;return AETHER_OK;
}
AETHER_API aether_status_t aether_style_runtime_commit_revision(
    aether_style_runtime_t runtime, aether_style_transaction_t transaction, aether_style_revision_t* out_revision) {
    if (out_revision) *out_revision = nullptr;
    if (!runtime || !transaction || !out_revision) return AETHER_ERROR_NULL_ARG;
    if (transaction->owner != runtime || transaction->base_pin!=runtime->revision ||
        transaction->candidate->registry!=style_property_registry_pin() || transaction->committed || runtime->preparing ||
        runtime->owner_thread != std::this_thread::get_id() || runtime->generation != transaction->base_generation ||
        runtime->sheet_epoch != transaction->base_sheet_epoch || !transaction->document_lifetime->alive ||
        transaction->document_lifetime->mutation_epoch != transaction->input_epoch ||
        transaction->document_lifetime->style_tree_epoch != transaction->base_style_tree_epoch) return AETHER_ERROR_INVALID_STATE;
    // No callbacks, allocation, comparison, diagnostics or fallible work in this
    // publication boundary. Old generated storage stays owned by retained frames.
    for (auto& link : transaction->links) {
        for (auto& old : link.host->generated_storage) aether_node_mark_attached(old.get(), false);
        link.host->children.swap(link.children); link.host->generated_storage.swap(link.storage);
        for (auto& current : link.host->generated_storage) aether_node_mark_attached(current.get(), true);
    }
    transaction->document_lifetime->style_tree_epoch = transaction->revision->style_tree_epoch();
    runtime->states.swap(transaction->candidate->states);
    runtime->document_measures.swap(transaction->candidate->document_measures);
    runtime->environment = transaction->revision->environment();
    runtime->generation = transaction->revision->generation();
    runtime->revision = transaction->revision;
    runtime->registry=transaction->candidate->registry;runtime->revision_registry=runtime->registry;
    runtime->revision_compiled = transaction->candidate->compiled;
    runtime->revision_sheet_epoch = transaction->base_sheet_epoch;
    transaction->committed = true;
    transaction->acknowledge_full_publication();
    count_work(WorkCounter::revision_publications);
    *out_revision = transaction->output.release();
    // Consumer reentrancy may publish another revision. Outputs above remain
    // those prepared by this operation; never read/write runtime after replay.
    try { transaction->diagnostics->replay(); } catch (...) { /* C callback must not unwind through C ABI. */ }
    return AETHER_OK;
}
AETHER_API void aether_style_transaction_destroy(aether_style_transaction_t transaction) { delete transaction; }
static int32_t resolve_legacy_atomic(aether_style_runtime_t runtime, aether_node_t root,
    const aether_style_environment_t* environment, aether_style_update_t** out_updates, bool isolate_legacy_root) {
    if (out_updates) *out_updates = nullptr;
    if (!runtime || !root || !out_updates) return 0;
    aether_style_transaction_t raw = nullptr;
    if (prepare_revision_impl(runtime, root, environment, &raw, isolate_legacy_root, true) != AETHER_OK) return 0;
    std::unique_ptr<aether_style_transaction_s> transaction(raw);
    aether_style_revision_t revision = nullptr;
    if (aether_style_runtime_commit_revision(runtime, transaction.get(), &revision) != AETHER_OK) return 0;
    aether_style_revision_destroy(revision);
    const int32_t count = static_cast<int32_t>(transaction->updates.size());
    *out_updates = transaction->legacy_array.release();
    for (auto& update : transaction->updates) update.release();
    return count;
}
AETHER_API aether_status_t aether_style_runtime_retain_revision(aether_style_runtime_t runtime, aether_style_revision_t* out) {
    if (out) *out = nullptr;
    if (!runtime || !out) return AETHER_ERROR_NULL_ARG;
    if (!runtime->revision || !runtime->revision->contextual()) return AETHER_ERROR_INVALID_STATE;
    try { auto handle = std::make_unique<aether_style_revision_handle_s>(); handle->value = runtime->revision; *out = handle.release(); return AETHER_OK; }
    catch (...) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
AETHER_API aether_status_t aether_style_revision_clone(aether_style_revision_t revision, aether_style_revision_t* out) {
    if (out) *out = nullptr;
    if (!revision || !out) return AETHER_ERROR_NULL_ARG;
    try { auto handle = std::make_unique<aether_style_revision_handle_s>(); handle->value = revision->value; *out = handle.release(); return AETHER_OK; }
    catch (...) { return AETHER_ERROR_OUT_OF_MEMORY; }
}
AETHER_API void aether_style_revision_destroy(aether_style_revision_t revision) { delete revision; }
AETHER_API uint64_t aether_style_revision_generation(aether_style_revision_t revision) { return revision ? revision->value->generation() : 0; }
AETHER_API uint64_t aether_style_revision_document_identity(aether_style_revision_t revision) { return revision ? revision->value->document_identity() : 0; }
AETHER_API int32_t aether_style_revision_style_count(aether_style_revision_t revision) { return revision ? static_cast<int32_t>(revision->value->entries().size()) : 0; }
AETHER_API int32_t aether_style_revision_change_count(aether_style_revision_t revision) { return revision ? static_cast<int32_t>(revision->value->changes().size()) : 0; }
static aether_status_t revision_get_entry(aether_style_revision_t revision, RevisionEntriesView entries,
    int32_t index,aether_node_t* out_node,aether_computed_style_handle_t* out_style,uint32_t* out_impact) {
    if(out_node)*out_node=nullptr;
    if(out_style)*out_style=nullptr;
    if(out_impact)*out_impact=0;
    if(!revision)return AETHER_ERROR_NULL_ARG;
    if(index<0||static_cast<size_t>(index)>=entries.size())return AETHER_ERROR_INVALID_STATE;
    const auto& entry=entries[static_cast<size_t>(index)];
    if(out_style&&entry.payload()) {
        auto handle=std::unique_ptr<aether_computed_style_handle_s>(new(std::nothrow)aether_computed_style_handle_s());
        if(!handle)return AETHER_ERROR_OUT_OF_MEMORY;
        FrozenNodeRef record;auto bind_status=BoundNodeView::find(revision->value,entry.node()).export_record(record);
        if(bind_status!=AETHER_OK)return bind_status;
        auto status=computed_style_deep_copy(record->style_view(),&handle->style);if(status!=AETHER_OK)return status;
        handle->record=record;handle->measures=record->measures();handle->revision=revision->value;
        handle->document_identity=record->document_identity();handle->node_identity=record->node_identity();
        handle->document_lifetime=record->document_lifetime();handle->node_lifetime=record->node_lifetime();
        *out_style=handle.release();
    }
    if(out_node)*out_node=entry.node();
    if(out_impact)*out_impact=entry.impact();
    return AETHER_OK;
}
AETHER_API aether_status_t aether_style_transaction_get_state_delta(
    aether_style_transaction_t transaction,int32_t index,aether_node_t* out_node,
    aether_computed_style_handle_t* out_style,uint32_t* out_impact) {
    if(out_node)*out_node=nullptr;if(out_style)*out_style=nullptr;if(out_impact)*out_impact=0;
    if(!transaction)return AETHER_ERROR_NULL_ARG;
    if(!transaction->state_operation||index<0||static_cast<size_t>(index)>=transaction->semantic_delta_positions.size())
        return AETHER_ERROR_INVALID_STATE;
    aether_style_revision_handle_s handle{transaction->revision};
    return revision_get_entry(&handle,transaction->revision->changes(),
        static_cast<int32_t>(transaction->semantic_delta_positions[static_cast<size_t>(index)]),out_node,out_style,out_impact);
}
AETHER_API aether_status_t aether_style_revision_get_style(aether_style_revision_t revision, int32_t index,
    aether_node_t* node, aether_computed_style_handle_t* style, uint32_t* impact) {
    return revision_get_entry(revision, revision ? revision->value->entries() : RevisionEntriesView{}, index, node, style, impact);
}
AETHER_API aether_status_t aether_style_revision_get_change(aether_style_revision_t revision, int32_t index,
    aether_node_t* node, aether_computed_style_handle_t* style, uint32_t* impact) {
    return revision_get_entry(revision, revision ? revision->value->changes() : RevisionEntriesView{}, index, node, style, impact);
}
static aether_status_t create_revision_map(aether_style_revision_t revision,aether_style_map_t* out_map,bool legacy=false) {
    if(out_map)*out_map=nullptr;
    if(!revision||!out_map)return AETHER_ERROR_NULL_ARG;
    try {
        auto map=std::make_unique<aether_style_map_s>();const auto& r=*revision->value;
        if(legacy||!r.contextual()) {
            const auto status=materialize_legacy_revision_map(revision->value,map->current);
            if(status!=AETHER_OK)return status;*out_map=map.release();return AETHER_OK;
        }
        MapVersionInput input;input.backing=r.backing();input.revision=revision->value;
        input.mode=legacy||!r.contextual()?MapMode::legacy_complete:MapMode::contextual;
        input.document_identity=r.document_identity();input.generation=r.generation();input.document_lifetime=r.document_lifetime();
        input.structure_epoch=r.structure_epoch();input.input_epoch=r.input_epoch();input.style_tree_epoch=r.style_tree_epoch();
        auto status=make_map_version(input,map->current);if(status!=AETHER_OK)return status;
        *out_map=map.release();return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
AETHER_API aether_status_t aether_style_revision_create_map(aether_style_revision_t revision, aether_style_map_t* out_map) {
    if (out_map) *out_map = nullptr;
    if (!revision || !out_map) return AETHER_ERROR_NULL_ARG;
    if (!revision->value->contextual()) return AETHER_ERROR_INVALID_STATE;
    return create_revision_map(revision, out_map);
}
AETHER_API aether_status_t aether_style_runtime_create_resolved_style_map(aether_style_runtime_t runtime, aether_doc_t doc, aether_style_map_t* out_map) {
    if (out_map) *out_map = nullptr;
    if (!runtime || !doc || !out_map) return AETHER_ERROR_NULL_ARG;
    if (!runtime->revision || runtime->revision->document_identity() != aether_node_identity(doc)) return AETHER_ERROR_INVALID_STATE;
    aether_style_revision_handle_s handle{runtime->revision};
    const auto status = create_revision_map(&handle, out_map, true);
    return status;
}
AETHER_API aether_status_t aether_style_runtime_update_resolved_style_map(aether_style_runtime_t runtime,
    aether_style_map_t map,const aether_node_t* nodes,int32_t node_count) {
    if(!runtime||!map||(!nodes&&node_count>0))return AETHER_ERROR_NULL_ARG;
    const auto previous=pin_style_map(map);
    if(node_count<0||(previous&&previous->contextual()))return AETHER_ERROR_INVALID_STATE;
    try {
        std::vector<BackingPatchEntry> patch;
        patch.reserve(static_cast<size_t>(node_count));
        for(int32_t i=0;i<node_count;++i){auto* node=nodes[i];if(!node)continue;
            const auto* current=runtime->states.find(node);
            FrozenNodeRef record;
            if(current) {
                const auto view=BoundNodeView::find(runtime->revision,node);
                const auto old=previous&&previous->backing()?previous->backing()->find_owner(node):FrozenNodeRef{};
                const auto document=view.document_owner();
                if(old&&old->payload_owner()==view.payload_owner()&&old->measures()&&old->measures()->document_owner()==document&&
                    !old->measures()->document_owner().owner_before(document)&&!document.owner_before(old->measures()->document_owner()))record=old;
                else {const auto status=view.export_record(record);if(status!=AETHER_OK)return status;}
            }
            patch.push_back({node,std::move(record)});
        }
        MapVersionInput input;input.mode=MapMode::legacy_partial;
        if(previous){input.document_lifetime=previous->document_lifetime();input.document_identity=previous->document_identity();
            input.input_epoch=previous->input_epoch();input.structure_epoch=previous->structure_epoch();input.style_tree_epoch=previous->style_tree_epoch();}
        auto status=patch_backing(previous?previous->backing():BackingRef{},patch,input.backing);if(status!=AETHER_OK)return status;
        MapVersionRef next;status=make_map_version(input,next);if(status!=AETHER_OK)return status;
        map->current.swap(next);return AETHER_OK;
    }catch(const std::bad_alloc&){return AETHER_ERROR_OUT_OF_MEMORY;}catch(...){return AETHER_ERROR_INVALID_STATE;}
}
AETHER_API void aether_style_runtime_clear_cache(aether_style_runtime_t runtime) { if (runtime) { runtime->states.clear(); ++runtime->sheet_epoch; } }
