#include "serval/debug.h"
#include "serval/text.h"
#include "test.h"

#include "../src/core/warn.h"

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

// Initials as a game stores them: three characters, no terminating zero. On
// the host, ASan reports any read past them.
static const char initials[3] = {'Z', 'O', 'E'};

static void string_precision(void) {
    CHECK(equals(text_format("[%.3s]", initials), "[ZOE]"));
    CHECK(equals(text_format("[%.2s]", "hello"), "[he]"));
    CHECK(equals(text_format("[%.10s]", "hi"), "[hi]"));
    CHECK(equals(text_format("[%.0s][%.s]", "x", "y"), "[][]"));
    CHECK(equals(text_format("[%5.2s][%-5.2s]", "hello", "hello"), "[   he][he   ]"));
    CHECK(equals(text_format("[%.3s]", (const char*)0), "[(nu]"));
    CHECK(equals(text_format("[%.99999999999s]", "all"), "[all]"));
}

static void star_precision(void) {
    CHECK(equals(text_format("[%.*s]|%d", 2, "abc", 5), "[ab]|5"));
    CHECK(equals(text_format("[%.*s]", 3, initials), "[ZOE]"));
    CHECK(equals(text_format("[%.*s]", -1, "abc"), "[abc]")); // negative: no precision
    CHECK(equals(text_format("[%-4.*s]", 1, "abc"), "[a   ]"));
}

static void precision_ignored_on_other_conversions(void) {
    u32 warnings = debug_warning_count();
    CHECK(equals(text_format("%.2d %.3x %d", 5, 0xAu, 7), "5 a 7"));
    CHECK(equals(text_format("%.*d|%d", 4, 5, 6), "5|6")); // '*' still consumes its int
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1); // reported once
#else
    CHECK(debug_warning_count() == warnings);
#endif
}

// --- Warnings (serval_warnf, warn.h; debug builds only) ----------------------

#define FORTY "0123456789012345678901234567890123456789"
#define TWO_HUNDRED_FORTY FORTY FORTY FORTY FORTY FORTY FORTY

// Warnings have a buffer of their own, past text_format's 127 characters: a
// message of SERVAL_WARN_MAX - 1 (247) characters arrives whole.
static void long_warnings_arrive_whole(void) {
#ifdef SERVAL_DEBUG
    u32 warnings = debug_warning_count();
    serval_warnf("%s|%d|%x", TWO_HUNDRED_FORTY, -12, 0xABu);
    CHECK(length(serval_warn_text()) == 247);
    CHECK(equals(serval_warn_text(), TWO_HUNDRED_FORTY "|-12|ab"));
    CHECK(debug_warning_count() == warnings + 1);

    SERVAL_WARN("%s: sprite %u, frame %u", FORTY FORTY FORTY FORTY, 513u, 7u); // the macro's way
    CHECK(equals(serval_warn_text(), FORTY FORTY FORTY FORTY ": sprite 513, frame 7"));
    CHECK(debug_warning_count() == warnings + 2);
#endif
}

// Longer ones are cut after 247 characters, with a NUL after them, whether the
// cut falls in a conversion or in the format's own text; a shorter warning
// after them ends where it should.
static void longer_warnings_are_cut_at_247(void) {
#ifdef SERVAL_DEBUG
    serval_warnf("%s|%s", TWO_HUNDRED_FORTY, "0123456789");
    CHECK(length(serval_warn_text()) == 247);
    CHECK(equals(serval_warn_text(), TWO_HUNDRED_FORTY "|012345"));

    serval_warnf("%s and then plain text", TWO_HUNDRED_FORTY);
    CHECK(equals(serval_warn_text(), TWO_HUNDRED_FORTY " and th"));

    serval_warnf("short: %d", 5);
    CHECK(equals(serval_warn_text(), "short: 5"));
#endif
}

// Widths and %s precisions are capped at the largest buffer's size, not
// text_format's, so a warning gets them in full.
static void wide_fields_in_warnings(void) {
#ifdef SERVAL_DEBUG
    serval_warnf("%200d|", 5);
    const char* s = serval_warn_text();
    CHECK(length(s) == 201);
    CHECK(s[0] == ' ' && equals(s + 199, "5|"));

    serval_warnf("%.2000s", TWO_HUNDRED_FORTY FORTY); // 280 characters
    CHECK(equals(serval_warn_text(), TWO_HUNDRED_FORTY "0123456"));
#endif
}

// A problem with a warning's own format (here %lld, which prints only the low
// 32 bits) is reported while that warning is being formatted. It has a buffer
// of its own, so the first warning still arrives whole. This is the test run's
// first %ll, which text_format reports only once.
static void warning_while_formatting_a_warning(void) {
#ifdef SERVAL_DEBUG
    u32 warnings = debug_warning_count();
    serval_warnf("%s|%lld|%s", TWO_HUNDRED_FORTY, 5LL, "end");
    CHECK(equals(serval_warn_text(), TWO_HUNDRED_FORTY "|5|end"));
    CHECK(debug_warning_count() == warnings + 2);
#endif
}

// text_format keeps its own limit, TEXT_FORMAT_MAX - 1 (127) characters, for
// the same text and fields.
static void text_format_keeps_its_limit(void) {
    CHECK(
        equals(text_format("%s|%s", TWO_HUNDRED_FORTY, "0123456789"), FORTY FORTY FORTY "0123456"));
    CHECK(length(text_format("%200d|", 5)) == TEXT_FORMAT_MAX - 1);
    CHECK(equals(text_format("%.2000s", TWO_HUNDRED_FORTY), FORTY FORTY FORTY "0123456"));
}

// A NULL format string gives "" (reported once) instead of reading address 0.
static void null_format_gives_an_empty_string(void) {
    u32 warnings = debug_warning_count();
    const char* volatile none = NULL; // not a constant the compiler could see
    CHECK(equals(text_format(none), ""));
    CHECK(equals(text_format(none, 5), ""));
#ifdef SERVAL_DEBUG
    CHECK(debug_warning_count() == warnings + 1);
#else
    CHECK(debug_warning_count() == warnings);
#endif
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
           {"huge_width_is_capped", huge_width_is_capped}, {"string_precision", string_precision},
           {"star_precision", star_precision},
           {"precision_ignored_on_other_conversions", precision_ignored_on_other_conversions},
           {"long_warnings_arrive_whole", long_warnings_arrive_whole},
           {"longer_warnings_are_cut_at_247", longer_warnings_are_cut_at_247},
           {"wide_fields_in_warnings", wide_fields_in_warnings},
           {"warning_while_formatting_a_warning", warning_while_formatting_a_warning},
           {"text_format_keeps_its_limit", text_format_keeps_its_limit},
           {"null_format_gives_an_empty_string", null_format_gives_an_empty_string});
