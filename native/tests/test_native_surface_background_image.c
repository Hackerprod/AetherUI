/* External C11 surface background-image consumer: public header + production
   DSO only. No text measurement/rasterization in the consumer.
   Ciclo 02: background-image por la API pública de superficie (Etapa 2). */
#include <aether/aether.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "background_image_fixtures.h"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
#define OK(x) CHECK((x)==AETHER_OK)
static unsigned checks;
static void expected(aether_status_t got,aether_status_t want,const char* label){++checks;if(got!=want)fprintf(stderr,"FAIL %s: got %d expected %d\n",label,got,want);CHECK(got==want);}
static uint8_t* read_file(const char* path,size_t* size){FILE* f=fopen(path,"rb");long n;uint8_t* bytes;CHECK(f);CHECK(fseek(f,0,SEEK_END)==0);n=ftell(f);CHECK(n>0);CHECK(fseek(f,0,SEEK_SET)==0);*size=(size_t)n;bytes=(uint8_t*)malloc(*size);CHECK(bytes);CHECK(fread(bytes,1,*size,f)==*size);CHECK(fclose(f)==0);return bytes;}
static aether_surface_frame_info info(void){aether_surface_frame_info i;memset(&i,0xa5,sizeof(i));i.struct_size=sizeof(i);return i;}
static aether_surface_frame_info query(aether_surface_t s){aether_surface_frame_info i=info();OK(aether_surface_copy_frame(s,&i,NULL,0));return i;}
static uint64_t sequence_or_zero(aether_surface_t s){aether_surface_frame_info i=info();aether_status_t st=aether_surface_copy_frame(s,&i,NULL,0);if(st==AETHER_ERROR_NO_FRAME)return 0;CHECK(st==AETHER_OK);return i.sequence;}
static void metadata(aether_surface_frame_info i,int w,int h,uint64_t seq){CHECK(i.struct_size==sizeof(i)&&i.width==w&&i.height==h&&i.stride==(uint64_t)w*4&&i.byte_count==(uint64_t)w*h*4&&i.sequence==seq&&i.format==AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED);}
typedef struct {aether_surface_frame_info i;uint8_t* pixels;} Saved;
static Saved capture(aether_surface_t s){Saved r;r.i=query(s);r.pixels=(uint8_t*)malloc((size_t)r.i.byte_count);CHECK(r.pixels);OK(aether_surface_copy_frame(s,&r.i,r.pixels,(size_t)r.i.byte_count));return r;}
static void unchanged(aether_surface_t s,const Saved* old){Saved now=capture(s);metadata(now.i,old->i.width,old->i.height,old->i.sequence);CHECK(now.i.byte_count==old->i.byte_count&&!memcmp(now.pixels,old->pixels,(size_t)now.i.byte_count));free(now.pixels);}
static void release(Saved* s){free(s->pixels);s->pixels=NULL;}
static void pixel(const Saved* s,int x,int y,unsigned b,unsigned g,unsigned r,unsigned a){size_t at=(size_t)y*(size_t)s->i.stride+(size_t)x*4;const uint8_t* p=s->pixels+at;CHECK(x>=0&&y>=0&&x<s->i.width&&y<s->i.height);if(!(p[0]==b&&p[1]==g&&p[2]==r&&p[3]==a))fprintf(stderr,"pixel %d,%d got %u,%u,%u,%u expected %u,%u,%u,%u\n",x,y,p[0],p[1],p[2],p[3],b,g,r,a);CHECK(p[0]==b&&p[1]==g&&p[2]==r&&p[3]==a);}
static void save(const char* prefix,const char* suffix,const Saved* s){char path[4096];FILE* f;CHECK(snprintf(path,sizeof(path),"%s-%s.bgra",prefix,suffix)>0);f=fopen(path,"wb");CHECK(f);CHECK(fwrite(s->pixels,1,(size_t)s->i.byte_count,f)==(size_t)s->i.byte_count);CHECK(fclose(f)==0);}
static aether_status_t render(aether_surface_t s,const char* html,const aether_html_ltr_options* o){return aether_surface_render_html_ltr(s,html,strlen(html),o);}
static void fails(aether_surface_t s,const char* html,const aether_html_ltr_options* o,aether_status_t want,const char* label){Saved before=capture(s);expected(render(s,html,o),want,label);unchanged(s,&before);release(&before);}
static void succeeds(aether_surface_t s,const char* html,const aether_html_ltr_options* o,const char* label){uint64_t seq=sequence_or_zero(s);expected(render(s,html,o),AETHER_OK,label);CHECK(query(s).sequence==seq+1);}

static const char* s1_page="<html><body style=\"background-color:#f4f6fa;padding-left:24px;padding-top:24px;width:480px\"><div style=\"background-color:#ffffff;background-image:url('red1.png');background-repeat:no-repeat;padding-left:16px;padding-top:12px;padding-bottom:12px\"><h1 style=\"font-size:24px;font-weight:700;line-height:36px;color:#244466\">Iniciar sesión</h1><p>Correo electrónico</p></div></body></html>";
static const char* s2_page="<html><body><div style=\"background-image:url('rgbw2.png');background-size:4px 4px;padding-top:4px;padding-bottom:4px\"></div></body></html>";
static const char* spanish="<html><body style=\"background-color:#f4f6fa;padding-left:24px;padding-top:24px;width:480px\"><div style=\"background-color:#ffffff;padding-left:16px;padding-top:12px;padding-bottom:12px\"><h1 style=\"font-size:24px;font-weight:700;line-height:36px;color:#244466\">Iniciar sesión</h1><p>Correo electrónico</p><p>Contraseña</p><p>Íñigo: sesión válida</p></div></body></html>";
static const char* example_page="<html><body style=\"background-color:#f4f6fa;padding-left:24px;padding-top:24px;width:480px\"><div style=\"background-color:#ffffff;background-image:url('red1.png');background-repeat:no-repeat;padding-left:16px;padding-top:12px;padding-bottom:12px\"><h1 style=\"font-size:24px;font-weight:700;line-height:36px;color:#244466\">Iniciar sesión</h1><p>Correo electrónico</p><input type=\"text\" value=\"Ana\" style=\"width:160px;padding-left:8px;padding-right:8px;padding-top:6px;padding-bottom:6px;border-width:1px;border-style:solid;border-color:#223344\"><button type=\"button\">Entrar</button></div></body></html>";
static const uint8_t bad_bytes[]={0,1,2};
static aether_html_ltr_image_v1 red1_image={"red1.png",kRed1Png,sizeof(kRed1Png)};
static aether_html_ltr_image_v1 rgbw2_image={"rgbw2.png",kRgbw2Png,sizeof(kRgbw2Png)};
static aether_html_ltr_image_v1 huge_image={"red1.png",kHugeHeaderPng,sizeof(kHugeHeaderPng)};
static aether_html_ltr_image_v1 bad_image={"red1.png",bad_bytes,sizeof(bad_bytes)};

/* S8: variantes de la página S1. Cada variante sustituye la declaración
   correspondiente cuando S1 ya la tiene (background-image,
   background-repeat) y se añade cuando S1 no la tiene (background-position,
   background-size): css() rechaza declaraciones duplicadas. */
static char* div_page(const char* div_style){char* html=(char*)malloc(4096);CHECK(html);CHECK(snprintf(html,4096,"<html><body style=\"background-color:#f4f6fa;padding-left:24px;padding-top:24px;width:480px\"><div style=\"%s\"><h1 style=\"font-size:24px;font-weight:700;line-height:36px;color:#244466\">Iniciar sesión</h1><p>Correo electrónico</p></div></body></html>",div_style)>0);return html;}
static char* div_with_image(const char* image_value){char* style=(char*)malloc(1024);CHECK(style);CHECK(snprintf(style,1024,"background-color:#ffffff;background-image:%s;background-repeat:no-repeat;padding-left:16px;padding-top:12px;padding-bottom:12px",image_value)>0);return style;}
static char* div_with_repeat(const char* repeat_value){char* style=(char*)malloc(1024);CHECK(style);CHECK(snprintf(style,1024,"background-color:#ffffff;background-image:url('red1.png');background-repeat:%s;padding-left:16px;padding-top:12px;padding-bottom:12px",repeat_value)>0);return style;}
static char* div_with_extra(const char* extra){char* style=(char*)malloc(1024);CHECK(style);CHECK(snprintf(style,1024,"background-color:#ffffff;background-image:url('red1.png');background-repeat:no-repeat;padding-left:16px;padding-top:12px;padding-bottom:12px;%s",extra)>0);return style;}
static void s8_admitted(aether_surface_t s,const aether_html_ltr_options* o,const char* html,const char* label,unsigned br,unsigned bg,unsigned rr){Saved c;succeeds(s,html,o,label);c=capture(s);pixel(&c,24,24,br,bg,rr,255);release(&c);}
static void s8_rejected(aether_surface_t s,const aether_html_ltr_options* o,const char* html,const char* label){fails(s,html,o,AETHER_ERROR_UNSUPPORTED,label);}

int main(int argc,char** argv){
 size_t regular_size,bold_size;uint8_t* regular;uint8_t* bold;aether_surface_t s=NULL;aether_html_ltr_options o,with_red,with_rgbw2,with_both,v1;const char* prefix;Saved c;
 CHECK(argc==4);prefix=argv[3];regular=read_file(argv[1],&regular_size);bold=read_file(argv[2],&bold_size);
 memset(&o,0,sizeof(o));o.struct_size=sizeof(o);o.profile=AETHER_HTML_LTR_PROFILE_1;o.regular_font=regular;o.regular_font_size=regular_size;o.bold_font=bold;o.bold_font_size=bold_size;
 CHECK(aether_abi_version()==AETHER_ABI_VERSION_CURRENT&&AETHER_ABI_VERSION_GET_MINOR(aether_abi_version())==38);CHECK(aether_html_ltr_options_size()==sizeof(o));CHECK(sizeof(o)==72);CHECK(offsetof(aether_html_ltr_options,images)==56);
 with_red=o;with_red.images=&red1_image;with_red.image_count=1;
 with_rgbw2=o;with_rgbw2.images=&rgbw2_image;with_rgbw2.image_count=1;
 {static aether_html_ltr_image_v1 both[2];both[0]=red1_image;both[1]=rgbw2_image;with_both=o;with_both.images=both;with_both.image_count=2;}
 v1=o;v1.struct_size=(uint32_t)offsetof(aether_html_ltr_options,images);
 OK(aether_surface_create(640,320,&s));
 /* S1 — éxito perfil 1. */
 succeeds(s,s1_page,&with_red,"S1 red1");c=capture(s);pixel(&c,0,0,250,246,244,255);pixel(&c,639,319,250,246,244,255);pixel(&c,24,24,0,0,255,255);pixel(&c,25,24,255,255,255,255);pixel(&c,200,100,255,255,255,255);save(prefix,"s1",&c);release(&c);
 /* S2 — bilineal rgbw2 (matemática del ciclo 01 §7-B). */
 succeeds(s,s2_page,&with_rgbw2,"S2 rgbw2");c=capture(s);pixel(&c,0,0,0,0,255,255);pixel(&c,1,1,64,64,159,255);pixel(&c,5,5,64,64,159,255);save(prefix,"s2",&c);release(&c);
 /* S3 — rechazo sin registro (tamaño v1 y tamaño nuevo sin tabla). */
 fails(s,s1_page,&v1,AETHER_ERROR_UNSUPPORTED,"S3 v1 no registry");
 fails(s,s1_page,&o,AETHER_ERROR_UNSUPPORTED,"S3 empty table");
 /* S4 — bytes no decodificables. */
 {aether_html_ltr_options bad=o;bad.images=&bad_image;bad.image_count=1;fails(s,s1_page,&bad,AETHER_ERROR_UNSUPPORTED,"S4 bad bytes");}
 /* S5 — imagen gigante. */
 {aether_html_ltr_options bad=o;bad.images=&huge_image;bad.image_count=1;fails(s,s1_page,&bad,AETHER_ERROR_LIMIT_EXCEEDED,"S5 huge");}
 /* S6 — compatibilidad v1. */
 succeeds(s,spanish,&v1,"S6a spanish v1");
 fails(s,s1_page,&v1,AETHER_ERROR_UNSUPPORTED,"S6b image v1");
 {aether_html_ltr_options bad=o;bad.struct_size=57;fails(s,spanish,&bad,AETHER_ERROR_INVALID_ARGUMENT,"S6c struct 57");bad=o;bad.struct_size=73;fails(s,spanish,&bad,AETHER_ERROR_INVALID_ARGUMENT,"S6c struct 73");}
 {aether_html_ltr_options bad=o;bad.image_count=1;bad.images=NULL;fails(s,spanish,&bad,AETHER_ERROR_INVALID_ARGUMENT,"S6d count without table");}
 {aether_html_ltr_image_v1 e;const uint8_t* red=kRed1Png;
  e.source=NULL;e.bytes=red;e.byte_count=sizeof(kRed1Png);{aether_html_ltr_options bad=o;bad.images=&e;bad.image_count=1;fails(s,spanish,&bad,AETHER_ERROR_INVALID_ARGUMENT,"S6d null source");}
  e.source="";e.bytes=red;e.byte_count=sizeof(kRed1Png);{aether_html_ltr_options bad=o;bad.images=&e;bad.image_count=1;fails(s,spanish,&bad,AETHER_ERROR_INVALID_ARGUMENT,"S6d empty source");}
  e.source="red1.png";e.bytes=NULL;e.byte_count=sizeof(kRed1Png);{aether_html_ltr_options bad=o;bad.images=&e;bad.image_count=1;fails(s,spanish,&bad,AETHER_ERROR_INVALID_ARGUMENT,"S6d null bytes");}
  e.source="red1.png";e.bytes=red;e.byte_count=0;{aether_html_ltr_options bad=o;bad.images=&e;bad.image_count=1;fails(s,spanish,&bad,AETHER_ERROR_INVALID_ARGUMENT,"S6d zero bytes");}}
 /* S7 — perfil 2 + imagen (página del ejemplo). */
 {aether_html_ltr_options p2=with_both;p2.profile=AETHER_HTML_LTR_PROFILE_STATIC_CONTROLS_2;succeeds(s,example_page,&p2,"S7 example page");c=capture(s);pixel(&c,0,0,250,246,244,255);pixel(&c,24,24,0,0,255,255);pixel(&c,25,24,255,255,255,255);pixel(&c,500,30,255,255,255,255);save(prefix,"s7",&c);release(&c);}
 /* S8 — coherencia admisión/compilador. */
 {const char* red_images[]={"url(red1.png)","url('red1.png')"};
  for(size_t k=0;k<sizeof(red_images)/sizeof(red_images[0]);++k){char* style=div_with_image(red_images[k]);char* html=div_page(style);char label[64];snprintf(label,sizeof(label),"S8 image %zu",k);s8_admitted(s,&with_red,html,label,0,0,255);free(style);free(html);}
  {const char* repeats[]={"no-repeat"};
   for(size_t k=0;k<sizeof(repeats)/sizeof(repeats[0]);++k){char* style=div_with_repeat(repeats[k]);char* html=div_page(style);char label[64];snprintf(label,sizeof(label),"S8 repeat %zu",k);s8_admitted(s,&with_red,html,label,0,0,255);free(style);free(html);}}
  /* `repeat` solo con el tile 1x1 no cabe: 480x84 = 40320 tiles > 16384
     (regla R1 del ciclo 01: el desbordamiento rechaza en snapshot). Con
     tile 4x4 son 120x21 = 2520 tiles y el repeat sí renderiza. */
  {char* style=(char*)malloc(1024);CHECK(style);CHECK(snprintf(style,1024,"background-color:#ffffff;background-image:url('red1.png');background-repeat:repeat;background-size:4px 4px;padding-left:16px;padding-top:12px;padding-bottom:12px")>0);char* html=div_page(style);s8_admitted(s,&with_red,html,"S8 repeat 4px",0,0,255);free(style);free(html);}
  {const char* sizes[]={"background-size:cover","background-size:4px 4px"};
   for(size_t k=0;k<sizeof(sizes)/sizeof(sizes[0]);++k){char* style=div_with_extra(sizes[k]);char* html=div_page(style);char label[64];snprintf(label,sizeof(label),"S8 size %zu",k);s8_admitted(s,&with_red,html,label,0,0,255);free(style);free(html);}}
  {char* style=div_with_image("none");char* html=div_page(style);s8_admitted(s,&with_red,html,"S8 none",255,255,255);free(style);free(html);}
  {const char* positions[]={"background-position:center","background-position:10px 20%"};
   for(size_t k=0;k<sizeof(positions)/sizeof(positions[0]);++k){char* style=div_with_extra(positions[k]);char* html=div_page(style);char label[64];snprintf(label,sizeof(label),"S8 position %zu",k);s8_admitted(s,&with_red,html,label,255,255,255);free(style);free(html);}}
  {const char* bad_images[]={"url(a), url(b)","url(data:image/png;base64,AAAA)","url()","url('')"};
   for(size_t k=0;k<sizeof(bad_images)/sizeof(bad_images[0]);++k){char* style=div_with_image(bad_images[k]);char* html=div_page(style);char label[64];snprintf(label,sizeof(label),"S8 bad image %zu",k);s8_rejected(s,&with_red,html,label);free(style);free(html);}}
  {char* style=div_with_repeat("banana");char* html=div_page(style);s8_rejected(s,&with_red,html,"S8 bad repeat");free(style);free(html);}
  {const char* bad_extras[]={"background-size:banana","background-position:banana","background: url(red1.png)"};
   for(size_t k=0;k<sizeof(bad_extras)/sizeof(bad_extras[0]);++k){char* style=div_with_extra(bad_extras[k]);char* html=div_page(style);char label[64];snprintf(label,sizeof(label),"S8 bad extra %zu",k);s8_rejected(s,&with_red,html,label);free(style);free(html);}}
  {char html[4096];CHECK(snprintf(html,sizeof(html),"<html style=\"background-image:url(red1.png)\"><body><p>A</p></body></html>")>0);s8_rejected(s,&with_red,html,"S8 image on html");}}
 succeeds(s,spanish,&o,"final recovery");
 free(regular);free(bold);c=capture(s);save(prefix,"final",&c);release(&c);OK(aether_surface_destroy(s));printf("PASS surface background-image checks=%u\n",checks);return 0;
}
