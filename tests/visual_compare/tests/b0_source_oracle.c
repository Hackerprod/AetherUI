/* Independent installed-public-ABI C11 transport oracle. No CLI encoder reuse.
 * Usage: oracle HTML REGULAR_TTF BOLD_TTF WIDTH HEIGHT OUTPUT_RGBA [SOURCE IMAGE_PATH]...
 * With image pairs (source key + file) it uses create_v2; without them, create v1
 * exactly as before. Ciclo 01: las parejas alimentan background-image. */
#include <aether/aether_b0_source.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static unsigned char *load(const char *path, size_t *size) {
 FILE *f=fopen(path,"rb"); long n; unsigned char *p;
 if(!f || fseek(f,0,SEEK_END) || (n=ftell(f))<=0 || n>16*1024*1024 || fseek(f,0,SEEK_SET)) {if(f)fclose(f);return NULL;}
 p=(unsigned char*)malloc((size_t)n); if(!p){fclose(f);return NULL;}
 if(fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
 if(fclose(f)){free(p);return NULL;} *size=(size_t)n;return p;
}
static int dimension(const char *s) {char *end;long n=strtol(s,&end,10);return *s && !*end && n>0 && n<=2048?(int)n:0;}
static int valid(const aether_b0_source_frame_info_v1 *i,int w,int h) {
 return i->struct_size==sizeof(*i)&&i->version==1&&!i->reserved&&i->width==w&&i->height==h&&i->format==AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED&&i->stride==(uint64_t)w*4&&i->byte_count==(uint64_t)w*h*4&&i->sequence==1;
}
int main(int argc,char **argv) {
 unsigned char *html=NULL,*regular=NULL,*bold=NULL,*pixels=NULL; size_t hn=0,rn=0,bn=0; int rc=1,w,h; FILE *out=NULL;
 aether_b0_source_t session=NULL;
 aether_b0_source_options_v1 o={0};aether_b0_source_diagnostic_v1 d={0};aether_b0_source_frame_info_v1 q={0},c={0};
 aether_b0_source_image_v1 *images=NULL; unsigned char **image_bytes=NULL; size_t image_count=0;
 if(argc<7 || ((argc-7)%2) || !(w=dimension(argv[4])) || !(h=dimension(argv[5])))return 2;
 if(aether_b0_source_options_v1_size()!=sizeof(o)||aether_b0_source_diagnostic_v1_size()!=sizeof(d)||aether_b0_source_frame_info_v1_size()!=sizeof(q)||(aether_b0_source_capabilities()&7u)!=7u)return 3;
 html=load(argv[1],&hn);regular=load(argv[2],&rn);bold=load(argv[3],&bn);if(!html||!regular||!bold)goto done;
 image_count=(size_t)(argc-7)/2;
 if(image_count){
  if(aether_b0_source_options_v2_size()!=sizeof(aether_b0_source_options_v2))goto done;
  images=(aether_b0_source_image_v1*)calloc(image_count,sizeof(*images));image_bytes=(unsigned char**)calloc(image_count,sizeof(*image_bytes));
  if(!images||!image_bytes)goto done;
  for(size_t i=0;i<image_count;i++){
   size_t n=0;image_bytes[i]=load(argv[7+2*i+1],&n);if(!image_bytes[i])goto done;
   images[i].source=argv[7+2*i];images[i].bytes=image_bytes[i];images[i].byte_count=n;
  }
 }
 o.struct_size=sizeof(o);o.version=1;o.regular_font=regular;o.regular_font_size=rn;o.bold_font=bold;o.bold_font_size=bn;
 d.struct_size=sizeof(d);d.version=1;
 if(image_count){
  aether_b0_source_options_v2 o2={0};o2.struct_size=sizeof(o2);o2.version=2;
  o2.regular_font=regular;o2.regular_font_size=rn;o2.bold_font=bold;o2.bold_font_size=bn;
  o2.images=images;o2.image_count=image_count;
  if(aether_b0_source_create_v2(w,h,(const char*)html,hn,&o2,&session,&d)!=AETHER_OK)goto done;
 }else if(aether_b0_source_create(w,h,(const char*)html,hn,&o,&session,&d)!=AETHER_OK)goto done;
 memset(&d,0,sizeof(d));d.struct_size=sizeof(d);d.version=1;
 if(aether_b0_source_render(session,&d)!=AETHER_OK)goto done;
 q.struct_size=sizeof(q);q.version=1;
 if(aether_b0_source_copy_frame(session,&q,NULL,0)!=AETHER_OK||!valid(&q,w,h))goto done;
 pixels=(unsigned char*)malloc((size_t)q.byte_count);if(!pixels)goto done;
 c.struct_size=sizeof(c);c.version=1;
 if(aether_b0_source_copy_frame(session,&c,pixels,(size_t)q.byte_count)!=AETHER_OK||!valid(&c,w,h))goto done;
 for(size_t p=0;p<(size_t)c.byte_count;p+=4){unsigned b=pixels[p],g=pixels[p+1],r=pixels[p+2],a=pixels[p+3];unsigned channels[3]={r,g,b};for(unsigned k=0;k<3;k++){unsigned v=a?(channels[k]*255+a/2)/a:0;pixels[p+k]=(unsigned char)(v>255?255:v);}}
 if(aether_b0_source_destroy(session)!=AETHER_OK){session=NULL;goto done;}session=NULL;
 out=fopen(argv[6],"wb");if(!out)goto done;
 if(fwrite(pixels,1,(size_t)c.byte_count,out)!=(size_t)c.byte_count)goto done;
 if(fclose(out)){out=NULL;goto done;}out=NULL;rc=0;
 done: if(out)fclose(out);if(session&&aether_b0_source_destroy(session)!=AETHER_OK)rc=1;free(html);free(regular);free(bold);free(pixels);if(image_bytes){for(size_t i=0;i<image_count;i++)free(image_bytes[i]);free(image_bytes);}free(images);return rc;
}
