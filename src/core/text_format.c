#include "serval/text.h"

#include "warn.h"

#include <stdarg.h>

// Where formatted text goes: characters are written at `at` until it reaches
// `end`, the place kept for the terminating NUL; the rest are dropped.
typedef struct {
    char* at;
    char* end;
} Buffer;

static void put(Buffer* b, char c) {
    if (b->at < b->end)
        *b->at++ = c;
}

// The size of the largest buffer here, serval_warnf's (warn.h). A wider field
// or a longer precision could never be printed, so they are capped at it,
// which also keeps a huge one from overflowing or padding for billions of
// characters.
#define FIELD_MAX SERVAL_WARN_MAX

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

// The argument of an integer conversion with `longs` l modifiers: l reads a
// long (32 bits on the GBA), ll a long long, of which only the low 32 bits are
// printed.
static inline u32 integer_arg(va_list* args, u32 longs) {
    if (longs == 0)
        return va_arg(*args, unsigned);
    if (longs == 1)
        return (u32)va_arg(*args, unsigned long);
#ifdef SERVAL_DEBUG
    static bool warned;
    if (!warned) {
        warned = true;
        SERVAL_WARN("text_format: %%ll prints only the low 32 bits of a 64-bit value");
    }
#endif
    return (u32)va_arg(*args, unsigned long long);
}

// Prints a %s argument, at most `max` characters of it (reading no further).
static __attribute__((noinline)) void put_string(Buffer* b, va_list* args, u32 max, Field f) {
    const char* s = va_arg(*args, const char*);
    if (!s)
        s = "(null)";
#ifdef SERVAL_DEBUG
    // Usually a number passed for %s; reading it as a string would print
    // garbage or scan memory until a zero byte.
    if (!serval_plausible_pointer(s)) {
        static bool warned_string;
        if (!warned_string) {
            warned_string = true;
            SERVAL_WARN("text_format: the %%s argument (0x%x) is not a string", (u32)(uintptr_t)s);
        }
        s = "(?)";
    }
#endif
    u32 n = 0;
    while (n < max && s[n])
        n++;
    put_padded(b, s, n, f);
}

// A precision: its digits or '*' (an int argument; a negative one means none,
// as in printf); `p` points just past the '.'. Prints a %s with it and
// returns its 's'; on other conversions the precision is ignored (with a
// warning), and the conversion character is returned for convert() to go on.
static __attribute__((noinline)) const char* precision_conversion(Buffer* b, const char* p,
                                                                  va_list* args, Field f) {
    u32 precision = 0;
    if (*p == '*') {
        int n = va_arg(*args, int);
        precision = n < 0 ? 0xFFFFFFFFu : (u32)n;
        p++;
    } else {
        for (; *p >= '0' && *p <= '9'; p++) {
            if (precision < FIELD_MAX) // more could never be printed anyway
                precision = precision * 10 + (u32)(*p - '0');
        }
    }
    if (*p == 's') {
        put_string(b, args, precision, f);
        return p;
    }
#ifdef SERVAL_DEBUG
    static bool warned_precision;
    if (!warned_precision && *p != '\0') {
        warned_precision = true;
        SERVAL_WARN("text_format: a precision (\".N\") only applies to %%s; ignored for %%%c", *p);
    }
#endif
    return p;
}

// Formats one conversion; `p` points just past its '%'. Returns the last
// character of the conversion. Out of line, so the loop copying plain
// characters keeps its few variables in registers.
static __attribute__((noinline)) const char* convert(Buffer* b, const char* p, va_list* args) {
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
    while (*p >= '0' && *p <= '9') {
        if (f.width < FIELD_MAX)
            f.width = f.width * 10 + (u32)(*p - '0');
        p++;
    }
    if (f.width > FIELD_MAX)
        f.width = FIELD_MAX;
    Field text_field = {f.width, ' ', f.left}; // %s and %c never zero-pad

    // A precision ("%.3s", "%.*s") is handled out of line, keeping this
    // path's variables in registers.
    if (*p == '.') {
        p = precision_conversion(b, p + 1, args, text_field);
        if (*p == 's')
            return p;
    }

    // Length modifiers: h and hh arguments arrive as int anyway; l and ll go
    // to integer_arg.
    u32 longs = 0;
    if ((*p | 4) == 'l') { // 'h' or 'l' (0x68, 0x6C): one test on the usual path
        for (; *p == 'h' || *p == 'l'; p++)
            longs += *p == 'l';
    }

    char num[11];
    switch (*p) {
    case 'd':
    case 'i': {
        int v = (int)integer_arg(args, longs);
        u32 magnitude = v < 0 ? 0u - (u32)v : (u32)v;
        u32 n = decimal_digits(magnitude, num + 1);
        if (v < 0) {
            if (f.pad == '0') { // sign goes before zero padding: -007
                put(b, '-');
                f.width = f.width ? f.width - 1 : 0;
                put_padded(b, num + 1, n, f);
            } else {
                num[0] = '-';
                put_padded(b, num, n + 1, f);
            }
        } else {
            put_padded(b, num + 1, n, f);
        }
        break;
    }
    case 'u':
        put_padded(b, num, decimal_digits(integer_arg(args, longs), num), f);
        break;
    case 'x':
        put_padded(b, num, hex_digits(integer_arg(args, longs), num), f);
        break;
    case 's':
        put_string(b, args, 0xFFFFFFFFu, text_field);
        break;
    case 'c': {
        char c = (char)va_arg(*args, int);
        put_padded(b, &c, 1, text_field);
        break;
    }
    case '%':
        put(b, '%');
        break;
    case '\0': // lone '%' at the end
        return p - 1;
    default: {
        // Unsupported conversion: show it as written, but still consume
        // the argument of printf conversions, so the ones after it print
        // the right values.
        char c = *p;
        if (c == 'o' || c == 'X')
            (void)integer_arg(args, longs);
        else if (c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' || c == 'G' || c == 'a' ||
                 c == 'A')
            (void)va_arg(*args, double);
        else if (c == 'p' || c == 'n')
            (void)va_arg(*args, void*);
#ifdef SERVAL_DEBUG
        static bool warned_unknown;
        if (!warned_unknown) {
            warned_unknown = true;
            SERVAL_WARN("text_format: %%%c is not supported (use %%d %%i %%u %%x %%s %%c %%%%; "
                        "flags - and 0, a width, and a precision for %%s only: %%.3s)",
                        c);
        }
#endif
        put(b, '%');
        put(b, c);
        break;
    }
    }
    return p;
}

// Formats `fmt` into `out`, which holds `size` characters, NUL included;
// output past that is cut off. Inlined into text_format and serval_warnf, so
// text_format makes no extra call.
static inline __attribute__((always_inline)) void format(char* out, u32 size, const char* fmt,
                                                         va_list* args) {
    char* at = out;
    char* end = out + size - 1;
    for (const char* p = fmt; *p; p++) {
        if (*p != '%') {
            if (at < end)
                *at++ = *p;
            continue;
        }
        Buffer b = {at, end};
        p = convert(&b, p + 1, args);
        at = b.at;
    }
    *at = '\0';
}

const char* text_format(const char* fmt, ...) {
    static SERVAL_EWRAM_BSS char buffers[4][TEXT_FORMAT_MAX];
    static u32 next;
    char* out = buffers[next];
    next = (next + 1) % 4;

    va_list args;
    va_start(args, fmt);
    format(out, TEXT_FORMAT_MAX, fmt, &args);
    va_end(args);
    return out;
}

#ifdef SERVAL_DEBUG
// serval_warnf's buffers. Debug builds only: SERVAL_EWRAM_BSS puts them in the
// same section as text_format's buffers, so the linker couldn't drop them from
// a release game that prints text. The second takes a warning reported while
// the first is being formatted: one of this file's own, about the warning's
// format (an unsupported conversion, say), which would otherwise overwrite it.
static SERVAL_EWRAM_BSS char warn_buffers[2][SERVAL_WARN_MAX];
static SERVAL_EWRAM_BSS u32 warn_depth;

void serval_warnf(const char* fmt, ...) {
    char* out = warn_buffers[warn_depth ? 1 : 0];
    warn_depth++;

    va_list args;
    va_start(args, fmt);
    format(out, SERVAL_WARN_MAX, fmt, &args);
    va_end(args);

    warn_depth--;
    serval_warn(out);
}

const char* serval_warn_text(void) {
    return warn_buffers[0];
}
#endif
