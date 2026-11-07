#include "css/style_handle_export.h"
#include "css/style_map_types.h"
#include "css/computed_style_handle.h"
#include "dom/dom_types.h"
#include "frozen_style_test_support.h"
#include "frozen_backing_fault_support.h"
using namespace aether::style;
using frozen_test::need;
// Baseline C copy_background_layer copies the descriptor, nulls both pointers,
// then populates only the active kind. Pin the inactive descriptor too: semantic
// equality deliberately ignores it, and full frozen equality would dereference
// its historical nonzero count with null stops.
static void historical_inactive_descriptors(const aether_computed_style_t& copy) {
    need(copy.background_layers.count==3,"baseline layer count");
    const auto& image=copy.background_layers.items[1];
    need(image.kind==AETHER_BACKGROUND_LAYER_IMAGE_URL && image.image_source &&
        std::strcmp(image.image_source,"asset://immutable")==0,"baseline active image descriptor");
    need(image.gradient.stop_count==3 && image.gradient.stops==nullptr &&
        image.gradient.direction_x==.25f && image.gradient.direction_y==.75f,
        "baseline inactive image-gradient count/direction retained, stops null");
    const auto& gradient=copy.background_layers.items[2];
    need(gradient.kind==AETHER_BACKGROUND_LAYER_LINEAR_GRADIENT &&
        gradient.gradient.stop_count==3 && gradient.gradient.stops &&
        gradient.gradient.direction_x==.25f && gradient.gradient.direction_y==.75f,
        "baseline active gradient descriptor");
    need(gradient.image_source==nullptr,"baseline inactive present-empty image becomes null");
}
int main() {
    frozen_test::Builder builder;
    aether_doc_s source_doc, binding_doc;
    auto leaf=std::make_shared<aether_node_s>();leaf->is_generated=true;leaf->snapshot_owned=true;
    std::weak_ptr<aether_node_s> weak_leaf=leaf;
    NodeRecordInput ri;ri.node=leaf.get();ri.generated_leaf=leaf;ri.node_lifetime=leaf->identity;ri.node_identity=aether_node_identity(leaf.get());
    ri.document_lifetime=source_doc.identity;ri.document_identity=aether_node_identity(&source_doc);
    need(freeze_style(builder.flat,ri.style)==AETHER_OK,"freeze style");
    auto document=std::make_shared<const FrozenDocumentMeasureContext>(DocumentMeasureValues{800,600,16,23,41,ri.document_identity});
    MeasureScalarValues ms;ms.self_font_size=20;ms.line_height_kind=FrozenLineHeightKind::number;ms.line_height_value=1.5f;
    ms.row_gap={13,AETHER_CSS_UNIT_PX,0,0,0,0,0};ms.column_gap={17,AETHER_CSS_UNIT_PX,0,0,0,0,0};
    ri.measures=std::make_shared<const FrozenMeasures>(ms,document,ImmutableArray<MeasureBackgroundPosition>{},ImmutableArray<MeasureBackgroundSize>{},ImmutableArray<FrozenPropertyDependency>{});
    FrozenNodeRef record;need(freeze_node_record(ri,record)==AETHER_OK,"record");
    BackingBuildInput bi;bi.entries.emplace(leaf.get(),record);MapVersionInput mi;need(freeze_backing(bi,mi.backing)==AETHER_OK,"backing");
    mi.document_lifetime=binding_doc.identity;mi.document_identity=aether_node_identity(&binding_doc);mi.input_epoch=123;
    MapVersionRef version;need(make_map_version(mi,version)==AETHER_OK,"partial mixed document");
    aether_style_map_s map;map.current=version;
    auto handle=aether_style_map_get(&map,leaf.get());need(handle && handle->record==record,"lookup owns record");
    frozen_test::equal(handle->style,builder.flat);
    need(handle->document_identity==mi.document_identity && handle->document_lifetime==binding_doc.identity,"map binding wins");
    float line=0;uint64_t generation=0,identity=0; aether_css_length row{},column{};
    need(aether_computed_style_contextual_metrics(handle,&line,&row,&column,&generation,&identity,nullptr)==AETHER_OK,"metrics");
    need(line==30 && row.value==13 && column.value==17 && generation==41 && identity==mi.document_identity,"mixed partial metrics");
    aether_computed_style_handle_t clone=nullptr;need(aether_computed_style_handle_clone(handle,&clone)==AETHER_OK,"clone");
    // Legacy C copy preserves active descriptors, not inactive gradient payloads.
    need(snapshot_test::style_equal(handle->style,clone->style),"historical clone semantics");
    historical_inactive_descriptors(clone->style);
    handle->style.background_layers.items[1].image_source[0]='X';handle->style.transforms.items[0].matrix_a=123;
    need(clone->style.background_layers.items[1].image_source[0]=='a' && clone->style.transforms.items[0].matrix_a==1,"clone nested independence");
    auto fresh=aether_style_map_get(&map,leaf.get());need(fresh,"fresh");frozen_test::equal(fresh->style,builder.flat);
    aether_computed_style_t flat{};need(aether_computed_style_copy(fresh,&flat)==AETHER_OK,"flat export");need(snapshot_test::style_equal(flat,builder.flat),"historical copy semantics");
    historical_inactive_descriptors(flat);
    flat.text_gradient.stops[0].color.r=0;need(fresh->style.text_gradient.stops[0].color.r==.9f,"flat independent");aether_computed_style_free(&flat);
    // Same value at another owner is not a member of this version.
    FrozenNodeRef other;need(freeze_node_record(ri,other)==AETHER_OK,"second record");
    auto output=clone;need(export_style_handle(*version,other,&output)==AETHER_ERROR_INVALID_STATE && output==clone,"membership exact");
    FrozenNodeRef fake(record.get(), [](const FrozenNodeRecord*){});
    need(export_style_handle(*version,fake,&output)==AETHER_ERROR_INVALID_STATE && output==clone,"control owner exact");fake.reset();
    for(auto mode:{fault::Mode::cpp,fault::Mode::c}) {
        bool terminal=false;
        for(long ordinal=0;ordinal<1000;++ordinal) {
            output=clone;fault::arm(mode,ordinal);auto status=export_style_handle(*version,record,&output);
            if(status==AETHER_OK) {
                need(!fault::hit,"terminal not swallowed");aether_computed_style_handle_destroy(output);
                need(fault::cpp_alloc==fault::cpp_free && fault::c_alloc==fault::c_free,"success balanced");fault::disarm();terminal=true;break;
            }
            need(fault::hit && status==AETHER_ERROR_OUT_OF_MEMORY && output==clone,"export OOM atomic");
            need(fault::cpp_alloc==fault::cpp_free && fault::c_alloc==fault::c_free,"export failure balanced");fault::disarm();
        }
        need(terminal,"OOM terminal");
    }
    map.current.reset();version.reset();mi={};bi.entries.clear();ri={};record.reset();other.reset();leaf.reset();
    need(!weak_leaf.expired(),"public handles retain authentic pseudo");
    aether_computed_style_handle_destroy(handle);aether_computed_style_handle_destroy(fresh);
    need(!weak_leaf.expired(),"clone alone retains pseudo");
    aether_computed_style_handle_destroy(clone);need(weak_leaf.expired(),"last clone releases pseudo");
    std::puts("immutable handle ABI/binding/alias/OOM/lifetime PASS");
}
