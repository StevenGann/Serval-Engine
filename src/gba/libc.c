// The few C library routines GCC may emit calls to, even in code that never
// calls them directly (struct copies, zero-initialized locals, loops it
// recognizes as strlen). Providing them here keeps newlib, and its license
// notices, out of every ROM. See docs/licensing.md.
//
// Compiled with -fno-builtin -fno-tree-loop-distribute-patterns so GCC does
// not turn these loops back into calls to the functions being defined.

#include <tonc_core.h>

#include <stddef.h>

void* memcpy(void* restrict dst, const void* restrict src, size_t n);
void* memset(void* dst, int c, size_t n);
void* memmove(void* dst, const void* src, size_t n);
int memcmp(const void* a, const void* b, size_t n);
size_t strlen(const char* s);

void* memcpy(void* restrict dst, const void* restrict src, size_t n) {
    return tonccpy(dst, src, n);
}

void* memset(void* dst, int c, size_t n) {
    return toncset(dst, (u8)c, n);
}

void* memmove(void* dst, const void* src, size_t n) {
    unsigned char* d = dst;
    const unsigned char* s = src;
    if (d <= s || d >= s + n)
        return tonccpy(dst, src, n); // tonccpy copies forward
    while (n--)
        d[n] = s[n];
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const unsigned char* x = a;
    const unsigned char* y = b;
    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i])
            return x[i] - y[i];
    }
    return 0;
}

size_t strlen(const char* s) {
    const char* p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}
