#include <stddef.h>

void *__real_malloc(size_t);
void *__wrap_malloc(size_t size) {
  return __real_malloc(size);
}

void *__real_calloc(size_t, size_t);
void *__wrap_calloc(size_t nmemb, size_t size) {
  return __real_calloc(nmemb, size);
}

void *__real_realloc(void *, size_t);
void *__wrap_realloc(void *ptr, size_t size) {
  return __real_realloc(ptr, size);
}

void *__real_reallocarray(void *, size_t, size_t);
void *__wrap_reallocarray(void *ptr, size_t nmemb, size_t size) {
  return __real_reallocarray(ptr, nmemb, size);
}

void __real_free(void *);
void __wrap_free(void *ptr) {
  __real_free(ptr);
}
