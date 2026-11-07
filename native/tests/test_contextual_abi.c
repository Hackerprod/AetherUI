#include <aether/aether.h>
#include <aether/aether_contextual_style.h>
#include <aether/aether_contextual_layout.h>
#include <aether/aether_contextual_transitions.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"FAIL contextual C ABI: %s line %d\n",#x,__LINE__);return 1; } } while(0)
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(aether_computed_style_t)==1352,"flat style size baseline");
_Static_assert(_Alignof(aether_computed_style_t)==8,"flat style alignment baseline");
_Static_assert(sizeof(aether_css_length)==28,"flat length size baseline");
_Static_assert(_Alignof(aether_css_length)==4,"flat length alignment baseline");
_Static_assert(sizeof(aether_paint_command_t)==200,"paint size baseline");
_Static_assert(offsetof(aether_computed_style_t,gap)==64,"gap offset baseline");
_Static_assert(offsetof(aether_computed_style_t,width)==184,"width offset baseline");
_Static_assert(offsetof(aether_computed_style_t,font_size)==1284,"font offset baseline");
_Static_assert(offsetof(aether_computed_style_t,line_height)==1292,"line-height offset baseline");
_Static_assert(offsetof(aether_computed_style_t,transforms)==888,"transform offset baseline");
_Static_assert(offsetof(aether_computed_style_t,transform_origin)==904,"origin offset baseline");
_Static_assert(offsetof(aether_computed_style_t,backdrop_filters)==744,"filter offset baseline");
_Static_assert(offsetof(aether_computed_style_t,z_index)==1328,"z-index offset baseline");
_Static_assert(offsetof(aether_computed_style_t,transition_timing)==1348,"transition offset baseline");
#endif
int main(void) {
    aether_doc_t doc=NULL; aether_style_runtime_t runtime=NULL;
    aether_style_transaction_t transaction=NULL; aether_style_revision_t revision=NULL;
    aether_style_environment_t environment={800,600,1,16,0,0,0};
    REQUIRE(aether_contextual_style_version()==1);
    REQUIRE(aether_contextual_transition_version()==1);
    REQUIRE(aether_doc_create(&doc)==AETHER_OK);
    REQUIRE(aether_doc_parse_html(doc,"<html><body><div id='x' style='row-gap:30px;column-gap:10px;font-size:20px;line-height:2em'></div></body></html>")==AETHER_OK);
    aether_node_t node=aether_doc_get_element_by_id(doc,"x");
    REQUIRE(aether_style_runtime_create(NULL,&environment,NULL,NULL,&runtime)==AETHER_OK);
    REQUIRE(aether_style_runtime_prepare_revision(runtime,aether_doc_get_document_element(doc),NULL,&transaction)==AETHER_OK);
    REQUIRE(aether_style_runtime_commit_revision(runtime,transaction,&revision)==AETHER_OK);
    aether_style_transaction_destroy(transaction);
    aether_style_map_t map=NULL;
    REQUIRE(aether_style_revision_create_map(revision,&map)==AETHER_OK);
    aether_computed_style_handle_t style=aether_style_map_get(map,node);
    REQUIRE(style!=NULL);
    float line=0; aether_css_length row={0},column={0}; uint64_t generation=0,document=0,identity=0;
    REQUIRE(aether_computed_style_contextual_metrics(style,&line,&row,&column,&generation,&document,&identity)==AETHER_OK);
    REQUIRE(row.value==30&&column.value==10&&line==40&&generation&&document&&identity);
    aether_computed_style_t flat={0};
    REQUIRE(aether_computed_style_copy(style,&flat)==AETHER_OK);
    REQUIRE(flat.gap==column.value); /* Compatibility scalar cannot encode both axes. */
    aether_computed_style_free(&flat);
    for(int i=0;i<100;++i) {
        aether_style_revision_t retained=NULL; aether_computed_style_handle_t copy=NULL;
        REQUIRE(aether_style_revision_clone(revision,&retained)==AETHER_OK);
        REQUIRE(aether_computed_style_handle_clone(style,&copy)==AETHER_OK);
        REQUIRE(aether_style_revision_generation(retained)==generation);
        aether_style_revision_destroy(retained);aether_computed_style_handle_destroy(copy);
    }
    aether_style_runtime_destroy(runtime);aether_style_map_destroy(map);aether_style_revision_destroy(revision);
    REQUIRE(aether_computed_style_contextual_metrics(style,&line,&row,&column,NULL,NULL,NULL)==AETHER_OK&&line==40);
    aether_computed_style_handle_destroy(style);aether_doc_destroy(doc);
    puts("contextual C ABI: flat offsets and complete handle lifetimes PASS");return 0;
}
