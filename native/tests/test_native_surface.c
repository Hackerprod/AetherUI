/* External C11 consumer: exclusively installed public declarations and DSO. */
#include <aether/aether.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define OK(x) CHECK((x)==AETHER_OK)
typedef struct { aether_doc_t doc; aether_style_runtime_t runtime; aether_style_map_t styles; aether_layout_engine_t engine; aether_layout_result_t layout; aether_display_list_builder_t builder; aether_display_list_t list; } Scene;
static aether_text_metrics_t measure(const char* text,const aether_computed_style_t* style,float max_width,void* user) {
    aether_text_metrics_t m={(float)strlen(text)*8,16,12};(void)style;(void)max_width;(void)user;return m;
}
static Scene scene(const char* html,const char* css,int w,int h) {
    Scene s={0}; OK(aether_doc_create(&s.doc));OK(aether_doc_parse_html(s.doc,html));
    aether_style_runtime_options_t options={0};aether_style_environment_t env={(float)w,(float)h,1,16,0,0,0};int64_t sheet=0;aether_style_update_t* updates=NULL;
    OK(aether_style_runtime_create(&options,&env,NULL,NULL,&s.runtime));
    OK(aether_style_runtime_add_stylesheet(s.runtime,"*{margin:0;padding:0;border:0;}html,body,div{display:block;}",0,"reset",&sheet,NULL));
    OK(aether_style_runtime_add_stylesheet(s.runtime,css,2,"consumer",&sheet,NULL));
    int32_t count=aether_style_runtime_resolve_subtree(s.runtime,aether_doc_get_document_element(s.doc),&updates);CHECK(count>=0);aether_style_updates_free(updates,count);
    OK(aether_style_runtime_create_resolved_style_map(s.runtime,s.doc,&s.styles));
    OK(aether_layout_create(measure,NULL,&s.engine));OK(aether_layout_layout(s.engine,s.doc,s.styles,(float)w,(float)h,NULL,NULL,&s.layout));
    OK(aether_display_list_builder_create(measure,NULL,&s.builder));OK(aether_display_list_build(s.builder,s.layout,NULL,&s.list));return s;
}
static void release(Scene* s) {aether_display_list_destroy(s->list);aether_display_list_builder_destroy(s->builder);aether_layout_result_destroy(s->layout);aether_layout_destroy(s->engine);aether_style_map_destroy(s->styles);aether_style_runtime_destroy(s->runtime);aether_doc_destroy(s->doc);memset(s,0,sizeof(*s));}
static aether_surface_frame_info info(void) {aether_surface_frame_info i;memset(&i,0xa5,sizeof(i));i.struct_size=sizeof(i);return i;}
static aether_surface_frame_info query(aether_surface_t s){aether_surface_frame_info i=info();OK(aether_surface_copy_frame(s,&i,NULL,0));return i;}
static void metadata(aether_surface_frame_info i,int w,int h,uint64_t seq) {CHECK(i.struct_size==sizeof(i));CHECK(i.width==w && i.height==h && i.stride==(uint64_t)w*4 && i.byte_count==(uint64_t)w*h*4 && i.sequence==seq && i.format==AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED);}
static void pixel(const unsigned char* p,int w,int x,int y,int b,int g,int r,int a) {p+=4*(y*w+x);if(!(p[0]==b && p[1]==g && p[2]==r && p[3]==a))fprintf(stderr,"pixel (%d,%d): got %u,%u,%u,%u expected %d,%d,%d,%d\n",x,y,p[0],p[1],p[2],p[3],b,g,r,a);CHECK(p[0]==b && p[1]==g && p[2]==r && p[3]==a);}
static void unchanged(aether_surface_t s,const unsigned char* bytes,aether_surface_frame_info old){unsigned char b[4096];aether_surface_frame_info i=info();CHECK(old.byte_count<=sizeof(b));OK(aether_surface_copy_frame(s,&i,b,sizeof(b)));metadata(i,old.width,old.height,old.sequence);CHECK(memcmp(bytes,b,(size_t)i.byte_count)==0);}
typedef struct {aether_surface_t s;aether_display_list_t list;} ThreadArgs;
static void* wrong_thread(void* arg) {ThreadArgs* a=arg;unsigned char b[4096];aether_surface_frame_info i=info(),saved=i;memset(b,0xcc,sizeof(b));CHECK(aether_surface_resize(a->s,1,1)==AETHER_ERROR_WRONG_THREAD);CHECK(aether_surface_render(a->s,a->list)==AETHER_ERROR_WRONG_THREAD);CHECK(aether_surface_copy_frame(a->s,&i,b,sizeof(b))==AETHER_ERROR_WRONG_THREAD);CHECK(memcmp(&i,&saved,sizeof(i))==0);for(size_t n=0;n<sizeof(b);++n)CHECK(b[n]==0xcc);CHECK(aether_surface_destroy(a->s)==AETHER_ERROR_WRONG_THREAD);return NULL;}
static void save(const char* path,const unsigned char* p,int w,int h) {FILE* f=fopen(path,"wb");CHECK(f!=NULL);fprintf(f,"P6\n%d %d\n255\n",w,h);for(int i=0;i<w*h;++i){unsigned char rgb[3]={p[i*4+2],p[i*4+1],p[i*4]};CHECK(fwrite(rgb,1,3,f)==3);}CHECK(fclose(f)==0);}
int main(int argc,char** argv) {
    const char* html="<html><body><div id='solid'></div><div id='clip'><div id='alpha'></div></div></body></html>";
    const char* css="html,body{margin:0;padding:0;background:transparent}#solid{position:absolute;left:2px;top:2px;width:8px;height:8px;background:red}#clip{position:absolute;left:4px;top:4px;width:4px;height:4px;overflow:clip}#alpha{width:8px;height:8px;background:rgba(0,0,255,0.5)}";
    Scene a=scene(html,css,16,16), b=scene("<body><div></div></body>","body{margin:0}div{width:3px;height:3px;background:lime}",16,16);
    aether_surface_t s=NULL,out=(aether_surface_t)(uintptr_t)1;unsigned char buf[4098],saved[4096];aether_surface_frame_info i,old,sentinel;
    CHECK(aether_abi_version()==AETHER_ABI_VERSION_CURRENT && AETHER_ABI_VERSION_GET_MINOR(aether_abi_version())==38);CHECK(aether_abi_capabilities()&AETHER_CAPABILITY_NATIVE_SURFACE);CHECK(aether_surface_frame_info_size()==sizeof(i));
    CHECK(aether_surface_create(16,16,NULL)==AETHER_ERROR_NULL_ARG);CHECK(aether_surface_create(-1,1,&out)==AETHER_ERROR_INVALID_ARGUMENT && out==NULL);
    CHECK(aether_surface_create(8193,1,&out)==AETHER_ERROR_INVALID_ARGUMENT && out==NULL);CHECK(aether_surface_create(8192,2049,&out)==AETHER_ERROR_INVALID_ARGUMENT && out==NULL);
    OK(aether_surface_destroy(NULL));CHECK(aether_surface_resize(NULL,1,1)==AETHER_ERROR_NULL_ARG);CHECK(aether_surface_render(NULL,a.list)==AETHER_ERROR_NULL_ARG);i=info();sentinel=i;CHECK(aether_surface_copy_frame(NULL,&i,NULL,0)==AETHER_ERROR_NULL_ARG);CHECK(memcmp(&i,&sentinel,sizeof(i))==0);
    OK(aether_surface_create(16,16,&s));CHECK(s!=NULL);CHECK(aether_surface_render(s,NULL)==AETHER_ERROR_NULL_ARG);CHECK(aether_surface_copy_frame(s,NULL,NULL,0)==AETHER_ERROR_NULL_ARG);
    i=info();sentinel=i;CHECK(aether_surface_copy_frame(s,&i,NULL,0)==AETHER_ERROR_NO_FRAME);CHECK(memcmp(&i,&sentinel,sizeof(i))==0);memset(buf,0xcc,sizeof(buf));CHECK(aether_surface_copy_frame(s,&i,buf+1,sizeof(buf)-2)==AETHER_ERROR_NO_FRAME);CHECK(memcmp(&i,&sentinel,sizeof(i))==0);for(size_t n=0;n<sizeof(buf);++n)CHECK(buf[n]==0xcc);
    i.struct_size--;sentinel=i;CHECK(aether_surface_copy_frame(s,&i,NULL,0)==AETHER_ERROR_INVALID_ARGUMENT);CHECK(memcmp(&i,&sentinel,sizeof(i))==0);
    OK(aether_surface_render(s,a.list));i=query(s);metadata(i,16,16,1);
    memset(buf,0xcc,sizeof(buf));i=info();CHECK(aether_surface_copy_frame(s,&i,buf+1,1023)==AETHER_ERROR_BUFFER_TOO_SMALL);metadata(i,16,16,1);for(size_t n=0;n<sizeof(buf);++n)CHECK(buf[n]==0xcc);
    i=info();sentinel=i;CHECK(aether_surface_copy_frame(s,&i,NULL,1)==AETHER_ERROR_NULL_ARG);CHECK(memcmp(&i,&sentinel,sizeof(i))==0);
    i=info();OK(aether_surface_copy_frame(s,&i,buf+1,1024));CHECK(buf[0]==0xcc && buf[1025]==0xcc);metadata(i,16,16,1);
    for(int y=0;y<16;++y)for(int x=0;x<16;++x){if(x>=4&&x<8&&y>=4&&y<8)pixel(buf+1,16,x,y,128,0,127,255);else if(x>=2&&x<10&&y>=2&&y<10)pixel(buf+1,16,x,y,0,0,255,255);else pixel(buf+1,16,x,y,0,0,0,0);}
    if(argc>1)save(argv[1],buf+1,16,16);
    memset(buf,0xcc,sizeof(buf));i=info();OK(aether_surface_copy_frame(s,&i,buf+1,sizeof(buf)-2));CHECK(buf[0]==0xcc);for(size_t n=1025;n<sizeof(buf);++n)CHECK(buf[n]==0xcc);metadata(query(s),16,16,1);
    OK(aether_surface_render(s,b.list));metadata(query(s),16,16,2);old=info();OK(aether_surface_copy_frame(s,&old,saved,sizeof(saved)));OK(aether_surface_resize(s,20,12));unchanged(s,saved,old);metadata(query(s),16,16,2);OK(aether_surface_render(s,a.list));metadata(query(s),20,12,3);old=info();OK(aether_surface_copy_frame(s,&old,saved,sizeof(saved)));
    OK(aether_surface_resize(s,0,12));unchanged(s,saved,old);CHECK(aether_surface_render(s,a.list)==AETHER_ERROR_SUSPENDED);unchanged(s,saved,old);metadata(query(s),20,12,3);OK(aether_surface_resize(s,20,12));OK(aether_surface_render(s,a.list));metadata(query(s),20,12,4);old=info();OK(aether_surface_copy_frame(s,&old,saved,sizeof(saved)));
    CHECK(aether_surface_resize(s,-1,2)==AETHER_ERROR_INVALID_ARGUMENT);unchanged(s,saved,old);CHECK(aether_surface_resize(s,8193,1)==AETHER_ERROR_INVALID_ARGUMENT);unchanged(s,saved,old);CHECK(aether_surface_resize(s,8192,2049)==AETHER_ERROR_INVALID_ARGUMENT);unchanged(s,saved,old);OK(aether_surface_render(s,a.list));metadata(query(s),20,12,5);old=info();OK(aether_surface_copy_frame(s,&old,saved,sizeof(saved)));
    OK(aether_surface_resize(s,20,12));unchanged(s,saved,old);metadata(query(s),20,12,5);OK(aether_surface_render(s,a.list));metadata(query(s),20,12,6);
    old=info();OK(aether_surface_copy_frame(s,&old,saved,sizeof(saved)));CHECK(aether_surface_render(s,NULL)==AETHER_ERROR_NULL_ARG);unchanged(s,saved,old);
    {pthread_t t;ThreadArgs args={s,a.list};CHECK(pthread_create(&t,NULL,wrong_thread,&args)==0);CHECK(pthread_join(t,NULL)==0);unchanged(s,saved,old);}
    {Scene text=scene("<body>hello</body>","body{margin:0}",20,12);CHECK(aether_surface_render(s,text.list)==AETHER_ERROR_UNSUPPORTED);unchanged(s,saved,old);release(&text);}
    {Scene image=scene("<body><div></div></body>","body{margin:0}div{width:8px;height:8px;background-image:url(asset://test);background-size:8px 8px}",20,12);CHECK(aether_surface_render(s,image.list)==AETHER_ERROR_UNSUPPORTED);unchanged(s,saved,old);release(&image);}
    /* Actual main budget: clear + three 8192*2048 solid fills > 50M visits. */
    {Scene limit=scene("<body><div></div><div></div><div></div></body>","body{margin:0}div{position:absolute;left:0;top:0;width:8192px;height:2048px;background:red}",8192,2048);OK(aether_surface_resize(s,8192,2048));CHECK(aether_surface_render(s,limit.list)==AETHER_ERROR_LIMIT_EXCEEDED);unchanged(s,saved,old);release(&limit);}
    release(&a);release(&b);unchanged(s,saved,old);OK(aether_surface_destroy(s));
    for(int k=0;k<16;++k){Scene c=scene(html,css,16,16);OK(aether_surface_create(0,16,&s));CHECK(aether_surface_render(s,c.list)==AETHER_ERROR_SUSPENDED);i=info();CHECK(aether_surface_copy_frame(s,&i,NULL,0)==AETHER_ERROR_NO_FRAME);OK(aether_surface_resize(s,16,0));CHECK(aether_surface_render(s,c.list)==AETHER_ERROR_SUSPENDED);OK(aether_surface_resize(s,16,16));OK(aether_surface_render(s,c.list));release(&c);metadata(query(s),16,16,1);OK(aether_surface_destroy(s));}
    puts("PASS external C11 surface: analytic BGRA, state, canaries, resize/sequence, atomic errors, ownership/thread/lifetime");return 0;
}
