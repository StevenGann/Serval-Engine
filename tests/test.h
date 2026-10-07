#ifndef SERVAL_TEST_H
#define SERVAL_TEST_H

// Minimal test harness that runs both natively (tests/host) and on the GBA
// inside mGBA (tests/rom). It needs no C library, so it works in ROMs.

typedef struct {
    const char* name;
    void (*run)(void);
} TestCase;

typedef struct {
    const char* name;
    const TestCase* cases;
    unsigned count;
} TestSuite;

#define TEST_SUITE(var, label, ...)                                                                \
    static const TestCase var##_cases[] = {__VA_ARGS__};                                           \
    const TestSuite var = {label, var##_cases, sizeof(var##_cases) / sizeof(var##_cases[0])}

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond))                                                                               \
            test_fail(__FILE__, __LINE__, #cond);                                                  \
    } while (0)

// Records a failure of the current test case.
void test_fail(const char* file, int line, const char* expr);

// The number of failed checks so far.
unsigned test_failures(void);

// Runs every case of the suites and returns the number of failed checks.
unsigned test_run(const TestSuite* const* suites, unsigned count);

// Writes one line of output. Implemented by each runner.
void test_output(const char* line);

// Suites shared by all runners.
extern const TestSuite ecs_tests;
extern const TestSuite random_tests;
extern const TestSuite text_format_tests;
extern const TestSuite math_tests;
extern const TestSuite physics_tests;
extern const TestSuite map_tests;
extern const TestSuite anim_tests;
extern const TestSuite psg_sequencer_tests;
extern const TestSuite save_tests;
extern const TestSuite input_tests;
extern const TestSuite path_tests;
extern const TestSuite vm_tests;
extern const TestSuite splash_logic_tests;
extern const TestSuite color_tests;

// Planned API at run time (docs/development.md#planned-api).
extern const TestSuite planned_audio_tests;
extern const TestSuite planned_sprites_tests;
extern const TestSuite planned_map_tests;
extern const TestSuite planned_screen_tests;

// Host-only suites for the web backend's renderer and sound (src/web/).
extern const TestSuite web_ppu_tests;
extern const TestSuite web_apu_tests;

#endif // SERVAL_TEST_H
