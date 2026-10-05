// consumer_save: a minimal game that saves, built from a Serval Engine release
// archive by tests/consumer/CMakeLists.txt with a save type other than the
// default (SAVE EEPROM8K), so the archive is known to give games every save
// type. Writes a slot, reads it back and exits through debug_exit with the
// number of failed checks.
//
// Uses only Serval Engine's API; no third-party headers.

#include "serval/serval.h"

typedef struct {
    u32 boots;
    char name[8];
} Progress;

int main(void) {
    serval_init();
    int failures = 0;
    if (save_slot_count() != 8 || save_slot_capacity() != 496) {
        debug_log("consumer_save: not EEPROM8K's slots (8 of 496 bytes)");
        failures++;
    }
    Progress saved = {42, "SERVAL"}, loaded = {0, ""};
    if (!save_write(7, &saved, sizeof saved, 1) ||
        save_read(7, &loaded, sizeof loaded, 1) != SAVE_OK || loaded.boots != 42) {
        debug_log("consumer_save: the save didn't read back");
        failures++;
    }
    if (debug_warning_count() != 0) {
        debug_log("consumer_save: the engine reported warnings");
        failures++;
    }
    debug_log(failures == 0 ? "consumer_save: ok" : "consumer_save: FAILED");
    debug_exit(failures);
}
