// The litter: see litter.h.

#include "litter.h"

#include <serval/random.h>

const char* const litter_names[LITTER_NAME_COUNT] = {
    "ZURI",  "JABARI", "KITO",  "NIA",    "AMANI", "BAHATI", "CHUMA", "DUMA",
    "ENZI",  "FARU",   "LAINI", "HEKIMA", "IMANI", "JUA",    "KIFU",  "LULU",
    "MWEZI", "NURU",   "OLEA",  "PEMBE",  "KIOO",  "MWANGA", "TAUSI", "TUMAINI",
    "UHURU", "VUMBI",  "WAZO",  "ZAMA",   "YARA",  "ZAWADI", "MOSI",  "PILI",
};

static int trait(int a, int b, int range) {
    int v = random_range(0, 1) ? a : b;
    if (random_range(0, 7) == 0)
        v = random_range(0, range - 1);
    return v;
}

Coat litter_visitor_coat(void) {
    Coat c = {(u8)random_range(0, 3), (u8)random_range(0, 3), (u8)random_range(0, 2)};
    return c;
}

void litter_make(Litter* out, Coat mother, Coat father, u8 avoid_name) {
    *out = (Litter){0};
    out->count = (u8)random_range(2, 4);
    for (int i = 0; i < out->count; i++) {
        out->coats[i].shade = (u8)trait(mother.shade, father.shade, 4);
        out->coats[i].spots = (u8)trait(mother.spots, father.spots, 4);
        out->coats[i].eyes = (u8)trait(mother.eyes, father.eyes, 3);
        for (;;) {
            int n = random_range(0, LITTER_NAME_COUNT - 1);
            bool used = n == avoid_name;
            for (int j = 0; j < i; j++)
                used = used || out->names[j] == n;
            if (!used) {
                out->names[i] = (u8)n;
                break;
            }
        }
    }
}
