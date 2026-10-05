#include "serval/text.h"

#include <stdarg.h>

typedef struct {
    char* out;
    u32 len;
} Buffer;

static void put(Buffer* b, char c) {
    if (b->len < TEXT_FORMAT_MAX - 1)
        b->out[b->len++] = c;
}

// Field options parsed from a conversion such as "%-8s" or "%05d".
typedef struct {
    u32 width;
    char pad;  // ' ' or '0'
    bool left; // '-': pad on the right instead
} Field;

static void put_padded(Buffer* b, const char* s, u32 n, Field f) {
    if (!f.left) {
        for (u32 i = n; i < f.width; i++)
            put(b, f.pad);
    }
    for (u32 i = 0; i < n; i++)
        put(b, s[i]);
    if (f.left) {
        for (u32 i = n; i < f.width; i++)
            put(b, ' ');
    }
}

// Decimal digits of `value`, most significant first; returns the count. Uses
// repeated subtraction instead of division, which the GBA does in software.
static u32 decimal_digits(u32 value, char* out) {
    static const u32 powers[] = {1000000000, 100000000, 10000000, 1000000, 100000,
                                 10000,      1000,      100,      10,      1};
    u32 n = 0;
    for (u32 p = 0; p < 10; p++) {
        char digit = '0';
        while (value >= powers[p]) {
            value -= powers[p];
            digit++;
        }
        if (n || digit != '0' || p == 9) // skip leading zeros, keep a lone 0
            out[n++] = digit;
    }
    return n;
}

// Lowercase hex digits of `value`, most significant first; returns the count.
static u32 hex_digits(u32 value, char* out) {
    u32 n = 0;
    for (int shift = 28; shift >= 0; shift -= 4) {
        u32 nibble = (value >> shift) & 0xF;
        if (n || nibble || shift == 0)
            out[n++] = (char)(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
    return n;
}

const char* text_format(const char* fmt, ...) {
    static SERVAL_EWRAM_BSS char buffers[4][TEXT_FORMAT_MAX];
    static u32 next;
    Buffer b = {buffers[next], 0};
    next = (next + 1) % 4;

    va_list args;
    va_start(args, fmt);
    for (const char* p = fmt; *p; p++) {
        if (*p != '%') {
            put(&b, *p);
            continue;
        }
        p++;
        Field f = {0, ' ', false};
        for (;; p++) {
            if (*p == '-')
                f.left = true;
            else if (*p == '0')
                f.pad = '0';
            else
                break;
        }
        if (f.left)
            f.pad = ' '; // as in printf, '-' overrides '0'
        while (*p >= '0' && *p <= '9')
            f.width = f.width * 10 + (u32)(*p++ - '0');
        Field text_field = {f.width, ' ', f.left}; // %s and %c never zero-pad

        char num[11];
        switch (*p) {
        case 'd': {
            int v = va_arg(args, int);
            u32 magnitude = v < 0 ? 0u - (u32)v : (u32)v;
            u32 n = decimal_digits(magnitude, num + 1);
            if (v < 0) {
                if (f.pad == '0') { // sign goes before zero padding: -007
                    put(&b, '-');
                    f.width = f.width ? f.width - 1 : 0;
                    put_padded(&b, num + 1, n, f);
                } else {
                    num[0] = '-';
                    put_padded(&b, num, n + 1, f);
                }
            } else {
                put_padded(&b, num + 1, n, f);
            }
            break;
        }
        case 'u':
            put_padded(&b, num, decimal_digits(va_arg(args, unsigned), num), f);
            break;
        case 'x':
            put_padded(&b, num, hex_digits(va_arg(args, unsigned), num), f);
            break;
        case 's': {
            const char* s = va_arg(args, const char*);
            u32 n = 0;
            while (s[n])
                n++;
            put_padded(&b, s, n, text_field);
            break;
        }
        case 'c': {
            char c = (char)va_arg(args, int);
            put_padded(&b, &c, 1, text_field);
            break;
        }
        case '%':
            put(&b, '%');
            break;
        case '\0':
            p--; // lone '%' at the end
            break;
        default: // unknown conversion: show it as written
            put(&b, '%');
            put(&b, *p);
            break;
        }
    }
    va_end(args);

    b.out[b.len] = '\0';
    return b.out;
}
