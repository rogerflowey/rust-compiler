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
