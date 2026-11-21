/* External C11 surface gradient consumer: public header + production DSO only.
   Ciclo 03: gradientes por la API pública de superficie (Etapa 3, P2).
   Espejo de test_native_surface_background_image.c (helpers CHECK/expected/
   pixel/save/capture/fails/succeeds). BGRA en los .bgra y en las aserciones. */
#include <aether/aether.h>
#include <aether/aether_b0_source.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
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

/* G1: div 480x84 en (24,24) con gradiente 90deg 2 paradas (espejo de S1). */
static const char* g1_page="<html><body style=\"background-color:#f4f6fa;padding-left:24px;padding-top:24px;width:480px\"><div style=\"background-color:#ffffff;background-image:linear-gradient(in srgb 90deg, red, blue);background-repeat:no-repeat;padding-left:16px;padding-top:12px;padding-bottom:12px\"><h1 style=\"font-size:24px;font-weight:700;line-height:36px;color:#244466\">Iniciar sesión</h1><p>Correo electrónico</p></div></body></html>";
/* G2: gradiente en body, viewport completo 640x320 (propagación al canvas). */
static const char* g2_page="<html><body style=\"background-image:linear-gradient(in srgb 90deg, red, blue)\"></body></html>";
/* G5: div 200x200 en (24,24): div vacío con padding 100+100 = 200 (sin
 * texto: con h1/p el píxel central (124,124) caería sobre el glifo del
 * título y no sería gradiente puro). */
static const char* g5_page="<html><body style=\"background-color:#ffffff;padding-left:24px;padding-top:24px;width:200px\"><div style=\"background-color:#ffffff;background-image:linear-gradient(in srgb 90deg, red, blue);background-repeat:no-repeat;padding-top:100px;padding-bottom:100px\"></div></body></html>";
/* G8 área cero: div vacío sin padding (480x0) con gradiente. */
static const char* g8_zero_page="<html><body style=\"background-color:#ffffff;padding-left:24px;padding-top:24px;width:480px\"><div style=\"background-image:linear-gradient(in srgb 90deg, red, blue)\"></div></body></html>";
/* G-B0: misma página que el test B0 v2, con gradiente en lugar de imagen. */
static const char* g_b0_page=
    "<!doctype html><html lang=\"es\"><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<title>bg gradient</title><style>"
    "html{font-family:AetherB0;font-size:16px;font-weight:400;line-height:24px;"
    "color:#202020;white-space:nowrap}"
    "body{display:block}"
    ".box{display:block;width:100px;padding-top:20px;padding-bottom:20px;"
    "background-image:linear-gradient(in srgb 90deg, red, blue);background-repeat:no-repeat}"
    "</style></head><body><div class=\"box\"></div></body></html>";

static char* div_page(const char* div_style){char* html=(char*)malloc(4096);CHECK(html);CHECK(snprintf(html,4096,"<html><body style=\"background-color:#f4f6fa;padding-left:24px;padding-top:24px;width:480px\"><div style=\"%s\"><h1 style=\"font-size:24px;font-weight:700;line-height:36px;color:#244466\">Iniciar sesión</h1><p>Correo electrónico</p></div></body></html>",div_style)>0);return html;}
static char* div_with_image(const char* image_value){char* style=(char*)malloc(1024);CHECK(style);CHECK(snprintf(style,1024,"background-color:#ffffff;background-image:%s;background-repeat:no-repeat;padding-left:16px;padding-top:12px;padding-bottom:12px",image_value)>0);return style;}
static char* div_with_extra(const char* extra){char* style=(char*)malloc(1024);CHECK(style);CHECK(snprintf(style,1024,"background-color:#ffffff;background-image:linear-gradient(in srgb 90deg, red, blue);background-repeat:no-repeat;padding-left:16px;padding-top:12px;padding-bottom:12px;%s",extra)>0);return style;}
static void g8_admitted(aether_surface_t s,const aether_html_ltr_options* o,const char* html,const char* label,unsigned br,unsigned bg,unsigned rr){Saved c;succeeds(s,html,o,label);c=capture(s);pixel(&c,24,24,br,bg,rr,255);release(&c);}
static void g8_rejected(aether_surface_t s,const aether_html_ltr_options* o,const char* html,const char* label){fails(s,html,o,AETHER_ERROR_UNSUPPORTED,label);}

int main(int argc,char** argv){
 size_t regular_size,bold_size;uint8_t* regular;uint8_t* bold;aether_surface_t s=NULL;aether_html_ltr_options o,v1;const char* prefix;Saved c;
 CHECK(argc==4);prefix=argv[3];regular=read_file(argv[1],&regular_size);bold=read_file(argv[2],&bold_size);
 memset(&o,0,sizeof(o));o.struct_size=sizeof(o);o.profile=AETHER_HTML_LTR_PROFILE_1;o.regular_font=regular;o.regular_font_size=regular_size;o.bold_font=bold;o.bold_font_size=bold_size;
 CHECK(aether_abi_version()==AETHER_ABI_VERSION_CURRENT&&AETHER_ABI_VERSION_GET_MINOR(aether_abi_version())==38);CHECK(aether_html_ltr_options_size()==sizeof(o));CHECK(sizeof(o)==72);CHECK(offsetof(aether_html_ltr_options,images)==56);
 v1=o;v1.struct_size=(uint32_t)offsetof(aether_html_ltr_options,images);
 OK(aether_surface_create(640,320,&s));
 /* G1 — gradiente 90deg 2 paradas en div 480x84. */
 succeeds(s,g1_page,&o,"G1 gradient");c=capture(s);pixel(&c,0,0,250,246,244,255);pixel(&c,639,319,250,246,244,255);pixel(&c,24,24,0,0,255,255);pixel(&c,263,24,127,0,128,255);pixel(&c,264,24,128,0,127,255);pixel(&c,503,24,255,0,0,255);pixel(&c,24,107,0,0,255,255);pixel(&c,24,65,0,0,255,255);save(prefix,"g1",&c);release(&c);
 /* G2 — gradiente en body, viewport completo. */
 succeeds(s,g2_page,&o,"G2 body gradient");c=capture(s);pixel(&c,0,0,0,0,255,255);pixel(&c,319,160,127,0,128,255);pixel(&c,320,0,128,0,127,255);pixel(&c,639,319,255,0,0,255);save(prefix,"g2",&c);release(&c);
 /* G3 — rechazo: sin `in srgb` no es g1. */
 {char* style=div_with_image("linear-gradient(red, blue)");char* html=div_page(style);fails(s,html,&o,AETHER_ERROR_UNSUPPORTED,"G3 no srgb");free(style);free(html);}
 /* G4 — rechazo: valor truncado. */
 {char* style=div_with_image("linear-gradient(in srgb 90deg, red,");char* html=div_page(style);fails(s,html,&o,AETHER_ERROR_UNSUPPORTED,"G4 truncated");free(style);free(html);}
 /* G5 — div 200x200, gradiente 90deg 2 paradas. */
 succeeds(s,g5_page,&o,"G5 200x200");c=capture(s);pixel(&c,24,24,1,0,254,255);pixel(&c,124,124,128,0,127,255);pixel(&c,223,223,254,0,1,255);pixel(&c,24,223,1,0,254,255);pixel(&c,24,224,255,255,255,255);save(prefix,"g5",&c);release(&c);
 /* G6 — compatibilidad v1 (los gradientes no necesitan la tabla de imágenes). */
 succeeds(s,g1_page,&v1,"G6a g1 v1");
 {aether_html_ltr_options bad=o;bad.struct_size=57;fails(s,g1_page,&bad,AETHER_ERROR_INVALID_ARGUMENT,"G6c struct 57");bad=o;bad.struct_size=73;fails(s,g1_page,&bad,AETHER_ERROR_INVALID_ARGUMENT,"G6c struct 73");}
 /* G7 — perfil 2 + gradiente. */
 {aether_html_ltr_options p2=o;p2.profile=AETHER_HTML_LTR_PROFILE_STATIC_CONTROLS_2;succeeds(s,g1_page,&p2,"G7 profile 2");c=capture(s);pixel(&c,24,24,0,0,255,255);save(prefix,"g7",&c);release(&c);}
 /* G8 — coherencia admisión/compilador. */
 {char* style=div_with_image("linear-gradient(in srgb, red, blue)");char* html=div_page(style);g8_admitted(s,&o,html,"G8 no direction",2,0,253);free(style);free(html);}
 {char* style=div_with_image("linear-gradient(in srgb 37deg, red, blue)");char* html=div_page(style);g8_admitted(s,&o,html,"G8 37deg",48,0,207);free(style);free(html);}
 {char* style=div_with_extra("background-size:cover");char* html=div_page(style);g8_admitted(s,&o,html,"G8 size cover",0,0,255);free(style);free(html);}
 {char* style=div_with_extra("background-size:auto auto");char* html=div_page(style);g8_admitted(s,&o,html,"G8 size auto auto",0,0,255);free(style);free(html);}
 {succeeds(s,g8_zero_page,&o,"G8 zero area");c=capture(s);pixel(&c,24,24,255,255,255,255);save(prefix,"g8-zero",&c);release(&c);}
 {const char* bad_images[]={"linear-gradient(in srgb to top right, red, blue)","linear-gradient(in srgb 90deg, red)","linear-gradient(in srgb 90deg, red -10%, blue)","linear-gradient(in srgb 90deg, red 150%, blue)","linear-gradient(in srgb 90deg, red 10000px, blue)","linear-gradient(in srgb 90deg, red 2em, blue)","linear-gradient(in srgb 90deg, red calc(50% + 1em), blue)"};
  for(size_t k=0;k<sizeof(bad_images)/sizeof(bad_images[0]);++k){char* style=div_with_image(bad_images[k]);char* html=div_page(style);char label[64];snprintf(label,sizeof(label),"G8 bad image %zu",k);g8_rejected(s,&o,html,label);free(style);free(html);}}
 {char stops[1024];size_t n=0;CHECK(snprintf(stops,sizeof(stops),"linear-gradient(in srgb 90deg, ")>0);n=strlen(stops);
  for(int k=0;k<65;++k){CHECK(n+8<sizeof(stops));n+=(size_t)snprintf(stops+n,sizeof(stops)-n,"%s%s",k%2?"blue":"red",k==64?")":", ");}
  char* style=div_with_image(stops);char* html=div_page(style);g8_rejected(s,&o,html,"G8 65 stops");free(style);free(html);}
 {char* style=div_with_extra("background-size:200px 200px");char* html=div_page(style);g8_rejected(s,&o,html,"G8 explicit size");free(style);free(html);}
 {char* style=div_with_extra("background:linear-gradient(in srgb 90deg, red, blue)");char* html=div_page(style);g8_rejected(s,&o,html,"G8 shorthand");free(style);free(html);}
 /* G-B0 — la ruta B0 sigue rechazando el gradiente. Etapa observada: CSS
  * con razón CSS_LOSS — el runtime de estilos B0 descarta la declaración
  * linear-gradient (no la admite) y el chequeo de aceptación de hojas
  * (b0_source_stylesheets_accepted) la rechaza antes de llegar al
  * verify_style, que también la rechazaría (kind != IMAGE_URL). */
 {
  aether_b0_source_options_v2 bo;memset(&bo,0,sizeof(bo));bo.struct_size=sizeof(bo);bo.version=2;bo.regular_font=regular;bo.regular_font_size=regular_size;bo.bold_font=bold;bo.bold_font_size=bold_size;
  aether_b0_source_diagnostic_v1 d;memset(&d,0,sizeof(d));d.struct_size=sizeof(d);d.version=1;
  aether_b0_source_t bs=NULL;
  int rc=aether_b0_source_create_v2(200,100,g_b0_page,strlen(g_b0_page),&bo,&bs,&d);
  fprintf(stderr,"G-B0 diag: rc=%d status=%d stage=%u reason=%u\n",rc,d.status,d.stage,d.reason);
  CHECK(rc==AETHER_ERROR_UNSUPPORTED&&bs==NULL);
  CHECK(d.status==AETHER_ERROR_UNSUPPORTED&&d.stage==AETHER_B0_STAGE_CSS&&d.reason==AETHER_B0_REASON_CSS_LOSS);
 }
 succeeds(s,g1_page,&o,"final recovery");
 free(regular);free(bold);c=capture(s);save(prefix,"final",&c);release(&c);OK(aether_surface_destroy(s));printf("PASS surface gradient checks=%u\n",checks);return 0;
}
