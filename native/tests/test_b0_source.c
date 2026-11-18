#include <aether/aether_b0_source.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
static unsigned char* read_file(const char* path,size_t* n){FILE*f=fopen(path,"rb");assert(f);assert(fseek(f,0,SEEK_END)==0);long k=ftell(f);assert(k>=0);*n=(size_t)k;rewind(f);unsigned char*p=malloc(*n+1);assert(p);assert(fread(p,1,*n,f)==*n);p[*n]=0;assert(fclose(f)==0);return p;}
static aether_b0_source_diagnostic_v1 diag(void){aether_b0_source_diagnostic_v1 d={0};d.struct_size=sizeof(d);d.version=1;return d;}
static aether_b0_source_frame_info_v1 info(void){aether_b0_source_frame_info_v1 d={0};d.struct_size=sizeof(d);d.version=1;return d;}
static void* other_thread(void*p){aether_b0_source_t s=p;aether_b0_source_diagnostic_v1 d=diag(),old=d;aether_b0_source_frame_info_v1 i=info(),before=i;assert(aether_b0_source_render(s,&d)==AETHER_ERROR_INVALID_STATE);assert(!memcmp(&d,&old,sizeof(d)));assert(aether_b0_source_copy_frame(s,&i,NULL,0)==AETHER_ERROR_INVALID_STATE);assert(!memcmp(&i,&before,sizeof(i)));assert(aether_b0_source_destroy(s)==AETHER_ERROR_INVALID_STATE);return NULL;}
int main(int argc,char**argv){
 assert(argc==5);assert((aether_b0_source_capabilities() & AETHER_B0_SOURCE_STATIC_STYLESHEET_CONTROLS)!=0);assert(aether_b0_source_options_v1_size()==sizeof(aether_b0_source_options_v1));assert(aether_b0_source_diagnostic_v1_size()==sizeof(aether_b0_source_diagnostic_v1));assert(aether_b0_source_frame_info_v1_size()==sizeof(aether_b0_source_frame_info_v1));
 size_t hn,rn,bn;unsigned char*html=read_file(argv[1],&hn),*regular=read_file(argv[2],&rn),*bold=read_file(argv[3],&bn);
 aether_b0_source_options_v1 o={0};o.struct_size=sizeof(o);o.version=1;o.regular_font=regular;o.regular_font_size=rn;o.bold_font=bold;o.bold_font_size=bn;
 aether_b0_source_t s=NULL;aether_b0_source_diagnostic_v1 d=diag();int rc=aether_b0_source_create(640,400,(char*)html,hn,&o,&s,&d);printf("create=%d stage=%u reason=%u node=%llu\n",rc,d.stage,d.reason,(unsigned long long)d.node_ordinal);fflush(stdout);assert(rc==AETHER_OK&&s);assert(d.status==0&&d.stage==0&&d.reason==0&&d.node_ordinal==UINT64_MAX);
 free(html);free(regular);free(bold);
 aether_b0_source_frame_info_v1 i=info(),before=i;assert(aether_b0_source_copy_frame(s,&i,NULL,0)==AETHER_ERROR_NO_FRAME);assert(!memcmp(&i,&before,sizeof(i)));
 pthread_t t;assert(!pthread_create(&t,NULL,other_thread,s));assert(!pthread_join(t,NULL));
 d.version=2;before=i;assert(aether_b0_source_render(s,&d)==AETHER_ERROR_INVALID_ARGUMENT);assert(d.version==2);d=diag();assert(aether_b0_source_render(s,&d)==AETHER_OK);assert(d.stage==0);
 assert(aether_b0_source_copy_frame(s,&i,NULL,0)==AETHER_OK);assert(i.width==640&&i.height==400&&i.stride==2560&&i.byte_count==1024000&&i.sequence==1&&i.format==AETHER_PIXEL_FORMAT_BGRA8_PREMULTIPLIED);
 before=i;assert(aether_b0_source_copy_frame(s,&i,(uint8_t*)&i,sizeof(i))==AETHER_ERROR_INVALID_ARGUMENT);assert(!memcmp(&i,&before,sizeof(i)));
 i.version=2;before=i;assert(aether_b0_source_copy_frame(s,&i,NULL,0)==AETHER_ERROR_INVALID_ARGUMENT);assert(!memcmp(&i,&before,sizeof(i)));i.version=1;
 unsigned char*a=malloc((size_t)i.byte_count+32),*b=malloc((size_t)i.byte_count+32);assert(a&&b);memset(a,0xA5,(size_t)i.byte_count+32);memset(b,0x6B,(size_t)i.byte_count+32);
 assert(aether_b0_source_copy_frame(s,&i,a,1)==AETHER_ERROR_BUFFER_TOO_SMALL);for(size_t n=0;n<(size_t)i.byte_count+32;n++)assert(a[n]==0xA5);
 assert(aether_b0_source_copy_frame(s,&i,a,(size_t)i.byte_count+32)==AETHER_OK);assert(aether_b0_source_render(s,&d)==AETHER_OK);assert(aether_b0_source_copy_frame(s,&i,b,(size_t)i.byte_count+32)==AETHER_OK);assert(i.sequence==1&&!memcmp(a,b,(size_t)i.byte_count));
 for(size_t n=(size_t)i.byte_count;n<(size_t)i.byte_count+32;n++){assert(a[n]==0xA5);assert(b[n]==0x6B);}
 const size_t corner=(399u*640u+639u)*4u;assert(a[corner]==250&&a[corner+1]==246&&a[corner+2]==243&&a[corner+3]==255);
 FILE*out=fopen(argv[4],"wb");assert(out);assert(fwrite(a,1,(size_t)i.byte_count,out)==i.byte_count);assert(!fclose(out));free(a);free(b);assert(aether_b0_source_destroy(s)==AETHER_OK);assert(aether_b0_source_destroy(NULL)==AETHER_OK);puts("B0 SOURCE C11 PASS");return 0;
}
