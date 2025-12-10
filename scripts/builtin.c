typedef unsigned int size_t;

int printf(const char *pattern, ...);
int scanf(const char *pattern, ...);
void *malloc(size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memset(void* dest, int ch, size_t n);

void print(char *str) {
  printf("%s", str);
}

void println(char *str) {
  printf("%s\n", str);
}

void printInt(int n) {
  printf("%d", n);
}

void printlnInt(int n) {
  printf("%d\n", n);
}

char *getString() {
  char *buffer = malloc(256);
  scanf("%s", buffer);
  return buffer;
}

int getInt() {
  int n;
  scanf("%d", &n);
  return n;
}

void* builtin_memset(void* dest, int ch, size_t n) {
    return memset(dest, ch, n);
}

void* builtin_memcpy(void* dest, const void* src, size_t n) {
    return memcpy(dest, src, n);
}

void exit(int status){
    //fuck it
}

// builtin.c
#include <stdint.h>

void __builtin_array_repeat_copy(uint8_t *first_elem,
                                 int64_t elem_size,
                                 int64_t count) {
    // Nothing or trivial cases
    if (first_elem == 0 || elem_size <= 0 || count <= 1) {
        return;
    }

    // Copy element 0 into positions 1..count-1
    for (int64_t i = 1; i < count; ++i) {
        uint8_t *dst = first_elem + i * elem_size;
        uint8_t *src = first_elem;

        for (int64_t b = 0; b < elem_size; ++b) {
            dst[b] = src[b];
        }
    }
}
