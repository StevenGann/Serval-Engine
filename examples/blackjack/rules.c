// The shoe and the hand totals: plain C with no graphics, so the rules stay
// easy to check.

#include "game.h"

static u8 shoe[SHOE_CARDS];
static int shoe_pos;

// The cut card: the shoe is reshuffled between rounds once three quarters of
// it have been dealt, so card counters get less out of the last cards.
#define SHOE_CUT (SHOE_CARDS * 3 / 4)

void shoe_shuffle(void) {
    int n = 0;
    for (int deck = 0; deck < SHOE_DECKS; deck++)
        for (int suit = 0; suit < 4; suit++)
            for (int rank = 1; rank <= 13; rank++)
                shoe[n++] = CARD(rank, suit);
    // Fisher-Yates, seeded by random_entropy() when START was pressed.
    for (int i = SHOE_CARDS - 1; i > 0; i--) {
        int j = random_range(0, i);
        u8 t = shoe[i];
        shoe[i] = shoe[j];
        shoe[j] = t;
    }
    shoe_pos = 0;
}

bool shoe_needs_shuffle(void) {
    return shoe_pos >= SHOE_CUT;
}

u8 shoe_draw(void) {
    if (shoe_pos >= SHOE_CARDS) // can't happen: a round uses far fewer than 78 cards
        shoe_shuffle();
    return shoe[shoe_pos++];
}

int shoe_left(void) {
    return SHOE_CARDS - shoe_pos;
}

int card_value(u8 card) {
    int rank = CARD_RANK(card);
    return rank > 10 ? 10 : rank;
}

int hand_total(const Hand* h, bool* soft) {
    int total = 0;
    bool ace = false;
    for (int i = 0; i < h->count; i++) {
        total += card_value(h->cards[i]);
        ace |= CARD_RANK(h->cards[i]) == RANK_ACE;
    }
    // One ace may count 11 instead of 1 (two would make 22).
    bool s = ace && total + 10 <= 21;
    if (soft)
        *soft = s;
    return s ? total + 10 : total;
}

bool hand_blackjack(const Hand* h) {
    return h->count == 2 && hand_total(h, 0) == 21;
}

const char* hand_total_text(const Hand* h) {
    bool soft;
    int total = hand_total(h, &soft);
    if (total > 21)
        return text_format("BUST %d", total);
    if (soft && total < 21)
        return text_format("%d/%d", total - 10, total);
    return text_format("%d", total);
}
