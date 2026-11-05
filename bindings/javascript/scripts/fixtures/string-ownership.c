#include <aether/aether.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef _WIN32
#define TEST_API __declspec(dllexport)
#else
#define TEST_API
#endif

static int allocated, released, invalid_free, command_frees;
static void *live[128];
TEST_API char *review_alloc(const char *text) {
  if (!text) return NULL;
  size_t n = strlen(text) + 1;
  char *p = malloc(n);
  assert(p);
  memcpy(p,text,n);
  for (int i=0;i<128;i++) if(!live[i]) { live[i]=p; allocated++; return p; }
  abort();
}
TEST_API void review_free(void *p) {
  if (!p) return;
  for(int i=0;i<128;i++) if(live[i]==p) {
    live[i]=NULL; memset(p,0xCC,strlen(p)); free(p); released++; return;
  }
  invalid_free++;
}
TEST_API int review_allocated(void) { return allocated; }
TEST_API int review_released(void) { return released; }
TEST_API int review_invalid_free(void) { return invalid_free; }
TEST_API int review_command_frees(void) { return command_frees; }
TEST_API size_t review_paint_size(void) { return sizeof(aether_paint_command_t); }
TEST_API size_t review_paint_offset(void) { return offsetof(aether_paint_command_t, data); }
TEST_API size_t review_background_size(void) { return sizeof(aether_background_layer); }
TEST_API int review_paint(int tag, const char* text, aether_paint_command_t *p) {
  memset(p,0,sizeof(*p)); p->tag=tag;
  switch(tag) {
    case 4: p->data.draw_text.text=review_alloc(text); strcpy(p->data.draw_text.font_family,"á字体🙂"); break;
    case 5: p->data.draw_text_gradient.text=review_alloc(text); break;
    case 9: p->data.draw_text_shadow.text=review_alloc(text); break;
    case 10: p->data.draw_background_image.source=review_alloc(text); break;
    default:return -1;
  }
  return 0;
}
TEST_API void review_paint_free(aether_paint_command_t *p) {
  command_frees++;
  switch(p->tag) {
    case 4: review_free(p->data.draw_text.text); p->data.draw_text.text=NULL; break;
    case 5: review_free(p->data.draw_text_gradient.text); p->data.draw_text_gradient.text=NULL; break;
    case 9: review_free(p->data.draw_text_shadow.text); p->data.draw_text_shadow.text=NULL; break;
    case 10: review_free(p->data.draw_background_image.source); p->data.draw_background_image.source=NULL; break;
    default: break;
  }
}
TEST_API void review_background(const char *text,aether_background_layer *p) {
  memset(p,0,sizeof(*p)); p->kind=1; p->image_source=review_alloc(text);
}
TEST_API void review_background_free(aether_background_layer *p) { review_free(p->image_source); p->image_source=NULL; }
TEST_API void review_text_callback(aether_text_measurer_fn fn) {
  char *text=review_alloc("árbol 中文 🙂");
  aether_text_metrics_t result=fn(text,NULL,24.0f,NULL);
  assert(result.width==24.0f);
  review_free(text);
}
TEST_API void review_diagnostic_callback(aether_style_diagnostic_fn fn) {
  char *text=review_alloc("árbol 中文 🙂");
  fn(text,text,text,text,NULL);
  review_free(text);
}
TEST_API const char *review_status(void) { return "literal estático 中文 🙂"; }
