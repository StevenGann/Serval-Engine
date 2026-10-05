#include "serval/debug.h"
#include "serval/text.h"
#include "test.h"

static bool equals(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void plain_text_and_percent(void) {
    CHECK(equals(text_format("hello"), "hello"));
    CHECK(equals(text_format("100%%"), "100%"));
    CHECK(equals(text_format(""), ""));
}

static void integers(void) {
    CHECK(equals(text_format("%d", 0), "0"));
    CHECK(equals(text_format("%d", -42), "-42"));
    CHECK(equals(text_format("%d", -2147483647 - 1), "-2147483648"));
    CHECK(equals(text_format("%u", 4294967295u), "4294967295"));
    CHECK(equals(text_format("%x", 0xBEEFu), "beef"));
}

static void width_and_zero_padding(void) {
    CHECK(equals(text_format("[%5d]", 42), "[   42]"));
    CHECK(equals(text_format("[%05d]", 42), "[00042]"));
    CHECK(equals(text_format("[%04d]", -7), "[-007]"));
    CHECK(equals(text_format("[%4d]", -7), "[  -7]"));
    CHECK(equals(text_format("[%2d]", 12345), "[12345]")); // never truncates numbers
    CHECK(equals(text_format("[%4s]", "ab"), "[  ab]"));
}

static void accepts_fixed_width_types(void) {
    u32 big = 4000000000u;
    s32 negative = -5;
    CHECK(equals(text_format("%u %d", big, negative), "4000000000 -5"));
}

static void left_alignment(void) {
    CHECK(equals(text_format("[%-6s]", "UP"), "[UP    ]"));
    CHECK(equals(text_format("[%-4d]", -7), "[-7  ]"));
    CHECK(equals(text_format("[%-05u]", 42u), "[42   ]")); // '-' overrides '0'
    CHECK(equals(text_format("[%-2s]", "LONGER"), "[LONGER]"));
}

static void bad_string_arguments(void) {
    CHECK(equals(text_format("[%s]", (const char*)0), "[(null)]"));
#if defined(SERVAL_GBA) && defined(SERVAL_DEBUG)
    u32 warnings = debug_warning_count();
    CHECK(equals(text_format("[%s]", (const char*)0x00001234), "[(?)]")); // BIOS area, not a string
    CHECK(equals(text_format("[%s]", (const char*)0x00001234), "[(?)]"));
    CHECK(debug_warning_count() == warnings + 1); // once, not per call
#endif
}

static void strings_and_chars(void) {
    CHECK(equals(text_format("%s=%c", "key", 'v'), "key=v"));
}

static void buffers_rotate(void) {
    const char* a = text_format("%d", 1);
    const char* b = text_format("%d", 2);
    CHECK(equals(a, "1")); // still valid after another call
    CHECK(equals(b, "2"));
}

static void long_output_is_truncated(void) {
    const char* forty = "0123456789012345678901234567890123456789";
    const char* s = text_format("%s%s%s%s", forty, forty, forty, forty); // 160 characters
    unsigned n = 0;
    while (s[n])
        n++;
    CHECK(n == TEXT_FORMAT_MAX - 1);
}

static void unknown_conversion_is_shown(void) {
    u32 warnings = debug_warning_count();
    CHECK(equals(text_format("%q"), "%q"));
    CHECK(equals(text_format("end%"), "end"));
    int x = 0;
    // Unsupported printf conversions still consume their argument.
    CHECK(equals(text_format("%p %d", (void*)&x, 7), "%p 7"));
    CHECK(equals(text_format("%f %d", 1.5, 7), "%f 7"));
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1); // reported once
#else
    CHECK(debug_warning_count() == warnings);
#endif
}

static void length_modifiers_and_i(void) {
    CHECK(equals(text_format("%ld %d", -5L, 7), "-5 7"));
    CHECK(equals(text_format("%lu|%lx", 4000000000UL, 0xABCUL), "4000000000|abc"));
    CHECK(equals(text_format("%hd %hhu", 3, 4), "3 4"));
    CHECK(equals(text_format("%i %3i", -12, 5), "-12   5"));
}

static void width_on_hex_chars_and_strings(void) {
    CHECK(equals(text_format("[%4x]", 0xABu), "[  ab]"));
    CHECK(equals(text_format("[%04x]", 0xABu), "[00ab]"));
    CHECK(equals(text_format("[%3c][%-3c]", 'z', 'y'), "[  z][y  ]"));
    CHECK(equals(text_format("[%05s]", "ab"), "[   ab]")); // '0' doesn't apply to text
    CHECK(equals(text_format("[%03c]", 'q'), "[  q]"));
}

static unsigned length(const char* s) {
    unsigned n = 0;
    while (s[n])
        n++;
    return n;
}

// A huge width used to overflow (2^32 + 1 became 1) or pad for billions of
// characters.
static void huge_width_is_capped(void) {
    CHECK(length(text_format("%4294967297d|", 1)) == TEXT_FORMAT_MAX - 1);
    const char* s = text_format("%4000000000d|", 1);
    CHECK(length(s) == TEXT_FORMAT_MAX - 1);
    CHECK(s[0] == ' ');
    CHECK(equals(text_format("%99999999999999999999u", 2u) + TEXT_FORMAT_MAX - 2, " "));
    CHECK(length(text_format("%-4000000000s|", "x")) == TEXT_FORMAT_MAX - 1);
}

TEST_SUITE(text_format_tests, "text_format", {"plain_text_and_percent", plain_text_and_percent},
           {"integers", integers}, {"width_and_zero_padding", width_and_zero_padding},
           {"accepts_fixed_width_types", accepts_fixed_width_types},
           {"left_alignment", left_alignment}, {"bad_string_arguments", bad_string_arguments},
           {"strings_and_chars", strings_and_chars}, {"buffers_rotate", buffers_rotate},
           {"long_output_is_truncated", long_output_is_truncated},
           {"unknown_conversion_is_shown", unknown_conversion_is_shown},
           {"length_modifiers_and_i", length_modifiers_and_i},
           {"width_on_hex_chars_and_strings", width_on_hex_chars_and_strings},
           {"huge_width_is_capped", huge_width_is_capped});
