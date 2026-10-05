// Screens and the round: the title, betting, the deal, the dealer's peek,
// the player's actions on one or two hands, the dealer's turn and the
// settlement, with the HUD and the fades between screens. table.c animates
// what this file decides.
//
// The round is a sequence of steps, each waiting for the cards to stop
// moving (table_busy) and, where it should breathe, a few extra frames:
// `wait` counts those down before the state's next step runs.

#include "game.h"

typedef enum {
    TITLE,
    BETTING,
    DEALING, // the four opening cards
    PEEK,    // the dealer checks the hole card under an ace or a ten
    PLAYING, // the player acts on the active hand
    DEALER,  // the hole card turns over; the dealer draws to 17
    SETTLE,  // each hand's result and payout, one after the other
} State;

// Layout (pixels). The left column holds the bankroll, the labels, the
// totals and the bet's chips; cards fill the rest.
#define CARDS_X 64
#define DEALER_Y 20
#define PLAYER_Y 92
#define DEALER_RIGHT 200 // the shoe sits right of it
#define TABLE_RIGHT 236
#define SPLIT_X1 152 // the second hand's left edge after a split
#define BANNER_Y 72  // between the dealer's cards and the player's
#define STACK_X 20   // the bet's chip stack: its left edge and bottom
#define STACK_Y 150
#define BANK_X 4
#define BANK_Y 2
#define DEALER_CHIPS_X 120 // where lost chips go and winnings come from
#define DEALER_CHIPS_Y 30
#define BET_PILE_X 112 // the bet's chips while betting: in the middle
#define BET_PILE_Y 112

#define FADE_STEP 2
#define DEAL_GAP 14     // frames between opening cards
#define REPEAT_DELAY 16 // held-button repeat for the bet (button_repeat)
#define REPEAT_RATE 4

static State state;
static int step, wait;
static int brightness = SCREEN_BRIGHTNESS_MIN;
static bool fading_out;
static void (*next_screen)(void);

// The round.
static Hand dealer, hands[2];
static s8 dealer_ids[HAND_MAX], hand_ids[2][HAND_MAX];
static int bets[2];
static bool doubled[2];
static int hand_count, active;
static int selected;   // the highlighted action button
static int pressed;    // frames the chosen button shows pushed in
static int shown_bank; // the bankroll on screen, counting toward the real one
static int bank_bounce;
static int bet_bounce;
static int settle_hand;
static bool hole_shown;
static bool round_has_blackjack; // for the stats
static int stack_amount;         // chips on the table: the bets, then what comes back
static int title_ids[3];

static const u8 actions[4] = {BTN_HIT, BTN_STAND, BTN_DOUBLE, BTN_SPLIT};
static const u8 deal_button[1] = {BTN_DEAL};

// --- Fades -------------------------------------------------------------------

static void fade_to(void (*show)(void)) {
    next_screen = show;
    fading_out = true;
}

static bool update_fade(void) {
    if (fading_out) {
        brightness = int_max(brightness - FADE_STEP, SCREEN_BRIGHTNESS_MIN);
        if (brightness == SCREEN_BRIGHTNESS_MIN) {
            fading_out = false;
            next_screen();
        }
    } else {
        brightness = int_min(brightness + FADE_STEP, 0);
    }
    screen_set_brightness(brightness);
    return fading_out;
}

// The background drifts on a slow circle; the veil, moved half as far,
// slides over the paint. map_set_scroll moves each layer by itself (they are
// MAP_LAYER_FIXED, so the camera doesn't); the offsets count on rather than
// wrapping back, so the layers never jump.
static void drift_background(void) {
    u32 t = frame_count();
    int x = (int)(t / 2) + ((fx_cos((u16)(t * 150)) * 24) >> 8) + 64;
    int y = (int)(t / 4) + ((fx_sin((u16)(t * 150)) * 24) >> 8) + 64;
    map_set_scroll(3, x, y);
    map_set_scroll(2, x / 2, y / 2);
}

// --- Card positions ----------------------------------------------------------

static int fan_step(int count, int width, int widest) {
    if (count < 2)
        return widest;
    return int_min(widest, (width - CARD_W) / (count - 1));
}

static void player_card_pos(int h, int i, int* x, int* y) {
    int n = hands[h].count;
    if (hand_count == 1) {
        *x = CARDS_X + i * fan_step(n, TABLE_RIGHT - CARDS_X, 18);
    } else {
        int left = h == 0 ? CARDS_X - 4 : SPLIT_X1;
        *x = left + i * fan_step(n, 84, 16);
    }
    *y = PLAYER_Y - (hand_count == 2 && h == active && state == PLAYING ? 4 : 0);
}

static void dealer_card_pos(int i, int* x, int* y) {
    *x = CARDS_X + i * fan_step(dealer.count, DEALER_RIGHT - CARDS_X, 18);
    *y = DEALER_Y;
}

// Slides a hand's first `count` cards to their places (after a card was
// added or a split).
static void layout_cards(int h, int count) {
    for (int i = 0; i < count; i++) {
        int x, y;
        player_card_pos(h, i, &x, &y);
        table_move(hand_ids[h][i], x, y, 10);
    }
}

static void layout_hand(int h) {
    layout_cards(h, hands[h].count);
}

static void deal_player(int h, int delay) {
    Hand* hand = &hands[h];
    u8 card = shoe_draw();
    int i = hand->count++;
    hand->cards[i] = card;
    layout_cards(h, i); // the earlier cards make room
    int x, y;
    player_card_pos(h, i, &x, &y);
    hand_ids[h][i] = (s8)table_deal(card, x, y, true, delay);
}

static void deal_dealer(bool face_up, int delay) {
    u8 card = shoe_draw();
    int i = dealer.count++;
    dealer.cards[i] = card;
    for (int k = 0; k < i; k++) {
        int x, y;
        dealer_card_pos(k, &x, &y);
        table_move(dealer_ids[k], x, y, 10);
    }
    int x, y;
    dealer_card_pos(i, &x, &y);
    dealer_ids[i] = (s8)table_deal(card, x, y, face_up, delay);
}

// --- HUD ---------------------------------------------------------------------

// The bankroll counts toward its real value: fast for big changes, a tick
// per step of the count.
static void update_bank_count(void) {
    int target = (int)bankroll.bank;
    if (shown_bank == target)
        return;
    int diff = target - shown_bank;
    int stepv = diff / 10;
    if (stepv == 0)
        stepv = diff > 0 ? 1 : -1;
    shown_bank += stepv;
    bank_bounce = 2;
    if (frame_count() % 3 == 0)
        psg_play(SND_TICK);
}

static void draw_bank(void) {
    u16 color = shown_bank < (int)bankroll.bank   ? DIGITS_GOLD
                : shown_bank > (int)bankroll.bank ? DIGITS_RED
                                                  : DIGITS_WHITE;
    sprite_draw(SPR_DIGIT, DIGIT_CHIP, BANK_X, BANK_Y, DIGITS_GOLD);
    draw_number((u32)shown_bank, BANK_X + 9, BANK_Y, color, bank_bounce);
    if (bank_bounce > 0 && frame_count() % 4 == 0)
        bank_bounce--;
}

// A total in big digits: "7/17" for a soft hand, red when bust, gold at 21.
static void draw_total(const Hand* h, int x, int y) {
    if (h->count == 0)
        return;
    bool soft;
    int total = hand_total(h, &soft);
    u16 color = total > 21 ? DIGITS_RED : total == 21 ? DIGITS_GOLD : DIGITS_WHITE;
    if (soft && total < 21) {
        draw_number((u32)(total - 10), x, y, color, 0);
        int w = number_width((u32)(total - 10));
        sprite_draw(SPR_DIGIT, DIGIT_SLASH, x + w, y, color);
        draw_number((u32)total, x + w + 8, y, color, 0);
    } else {
        draw_number((u32)total, x, y, color, 0);
    }
}

// Totals on screen count the cards that have landed face up.
static Hand shown_cards(const Hand* hand, const s8* ids) {
    Hand h = {.count = 0};
    for (int i = 0; i < hand->count; i++)
        if (table_shown(ids[i]))
            h.cards[h.count++] = hand->cards[i];
    return h;
}

static void print_split_totals(void) {
    for (int h = 0; h < 2; h++) {
        bool soft;
        int total = hand_total(&hands[h], &soft);
        const char* t = total > 21           ? "BUST"
                        : soft && total < 21 ? text_format("%d/%d", total - 10, total)
                                             : text_format("%d", total);
        bool marked = h == active && state == PLAYING;
        text_print_line(0, 12 + h, text_format("%c%d %s", marked ? '>' : ' ', h + 1, t));
    }
}

static void draw_hud(void) {
    draw_bank();
    if (state == BETTING) {
        // The bet, big, in the middle of the table.
        int w = number_width((u32)bets[0]);
        draw_number((u32)bets[0], SCREEN_W / 2 - w / 2, 46, DIGITS_GOLD, bet_bounce);
        if (bet_bounce > 0 && frame_count() % 3 == 0)
            bet_bounce--;
        return;
    }
    Hand visible = shown_cards(&dealer, dealer_ids);
    draw_total(&visible, 8, 34);
    if (hand_count == 1) {
        visible = shown_cards(&hands[0], hand_ids[0]);
        draw_total(&visible, 8, 100);
    }
}

// The shoe: a pile of backs, thinner as it empties.
static void draw_shoe(void) {
    int layers = 1 + shoe_left() / 80;
    for (int i = 0; i < layers; i++)
        sprite_draw(SPR_CARD, CF_BACK, SHOE_X + i, SHOE_Y - 8 - i, 0);
}

// --- Screens -----------------------------------------------------------------

static void load_table_background(const Tileset* tileset) {
    tileset_load(tileset);
    drift_background();
    map_load(&swirl_layer);
    map_load(&ribbon_layer);
}

static void show_title(void) {
    state = TITLE;
    ecs_reset();
    table_reset();
    banner_clear();
    chips_set(0, STACK_X, STACK_Y, 0);
    load_table_background(&title_tileset);
    text_clear();
    banner_show("BLACKJACK", SCREEN_W / 2, 12, LETTERS_GOLD);
    text_print_centered(5, "A SERVAL ENGINE CARD TABLE");
    text_print_centered(15, text_format("BANK %u   BEST %u", bankroll.bank, bankroll.best));
    text_print_centered(17, "6 DECKS * BLACKJACK PAYS 3:2");
    text_print_centered(18, "DEALER STANDS ON ALL 17S");
    // A fan of three cards, dealt from below the screen.
    static const u8 fan[3] = {CARD(RANK_ACE, SUIT_SPADES), CARD(RANK_KING, SUIT_HEARTS),
                              CARD(RANK_JACK, SUIT_DIAMONDS)};
    for (int i = 0; i < 3; i++) {
        title_ids[i] = table_deal_from(fan[i], 104, SCREEN_H + 8, 72 + i * 32,
                                       54 + (i == 1 ? -4 : 2), true, 10 + i * 8);
        table_set_angle(title_ids[i], (i - 1) * 10);
    }
    psg_music_play(&lounge_song);
}

static void start_betting(void);

static void show_table(void) {
    ecs_reset();
    table_reset();
    banner_clear();
    load_table_background(&table_tileset);
    text_clear();
    hand_count = 1;
    hands[0].count = hands[1].count = 0;
    dealer.count = 0;
    start_betting();
}

// --- Betting -----------------------------------------------------------------

// The chips at stake: the bet, or the bets of both hands and doubles.
static void print_bet(int amount) {
    text_print_line(0, 19, text_format("BET %d", amount));
}

static int max_bet(void) {
    return int_min(BET_MAX, (int)bankroll.bank / BET_STEP * BET_STEP);
}

static void start_betting(void) {
    state = BETTING;
    step = 0;
    hand_count = 1;
    hands[0].count = hands[1].count = 0;
    dealer.count = 0;
    hole_shown = false;
    banner_clear();
    for (int row = 3; row < 19; row++)
        text_print_line(0, row, "");
    // Broke: the house stakes the player again.
    if ((int)bankroll.bank < BET_MIN) {
        bankroll.bank += BANK_START;
        bankroll.refills++;
        bank_save();
        banner_show("REFILL", SCREEN_W / 2, 70, LETTERS_SILVER);
        text_print_centered(3, "HOUSE STAKES YOU 1000");
        psg_play(SND_REFILL);
    }
    bets[0] = int_clamp(bankroll.bet, BET_MIN, max_bet());
    bets[0] = bets[0] / BET_STEP * BET_STEP;
    chips_set(bets[0], BET_PILE_X, BET_PILE_Y, 0);
    text_print_centered(4, "PLACE YOUR BET");
    text_print_centered(15, "LEFT/RIGHT:10  UP/DOWN:50");
    text_print_centered(16, "A:DEAL   START:TITLE");
    print_bet(bets[0]);
    selected = 0;
}

static void change_bet(int delta) {
    int bet = int_clamp(bets[0] + delta, BET_MIN, max_bet());
    if (bet == bets[0])
        return;
    bets[0] = bet;
    chips_set(bet, BET_PILE_X, BET_PILE_Y, 0);
    bet_bounce = 3;
    psg_play(delta > 0 ? SND_CHIP_UP : SND_CHIP_DOWN);
    print_bet(bets[0]);
}

static void deal_round(void);

static void update_betting(void) {
    // One step per press, then repeating while held (button_repeat).
    if (button_repeat(BUTTON_RIGHT | BUTTON_UP))
        change_bet(button_down(BUTTON_UP) ? 50 : BET_STEP);
    else if (button_repeat(BUTTON_LEFT | BUTTON_DOWN))
        change_bet(button_down(BUTTON_DOWN) ? -50 : -BET_STEP);
    if (button_pressed(BUTTON_START)) {
        fade_to(show_title);
        return;
    }
    if (button_pressed(BUTTON_A) && !table_busy()) {
        pressed = 8;
        psg_play(SND_PRESS);
        deal_round();
    }
}

// --- The round ---------------------------------------------------------------

static void deal_round(void) {
    bankroll.bet = (u16)bets[0];
    bankroll.bank -= (u32)bets[0];
    stack_amount = bets[0];
    // The pile slides over to the bet's place beside the hand.
    chips_fly(bets[0], BET_PILE_X, BET_PILE_Y - 12, STACK_X, STACK_Y - 12);
    chips_set(bets[0], STACK_X, STACK_Y, CHIPS_LANDING);
    for (int row = 4; row < 17; row++)
        text_print_line(0, row, "");
    banner_clear();
    text_print_line(0, 3, "");
    text_print(1, 3, "DEALER");
    text_print(1, 11, "YOU");
    doubled[0] = doubled[1] = false;
    round_has_blackjack = false;
    active = 0;
    state = DEALING;
    step = 0;
    wait = 0;
    if (shoe_needs_shuffle()) {
        shoe_shuffle();
        banner_show("SHUFFLE", 140, BANNER_Y, LETTERS_SILVER);
        psg_play(SND_SHUFFLE);
        wait = 60;
    }
}

static bool dealer_upcard_peeks(void) {
    int v = card_value(dealer.cards[0]);
    return v == 1 || v == 10;
}

static bool can_double(void) {
    const Hand* h = &hands[active];
    bool split_aces = hand_count == 2 && CARD_RANK(h->cards[0]) == RANK_ACE;
    return h->count == 2 && !split_aces && (int)bankroll.bank >= bets[active];
}

static bool can_split(void) {
    const Hand* h = &hands[0];
    return hand_count == 1 && h->count == 2 && CARD_RANK(h->cards[0]) == CARD_RANK(h->cards[1]) &&
           (int)bankroll.bank >= bets[0];
}

static u16 enabled_actions(void) {
    return (u16)(1 | 2 | (can_double() ? 4 : 0) | (can_split() ? 8 : 0));
}

static void start_dealer(void) {
    state = DEALER;
    step = 0;
    wait = 10;
    if (hand_count == 2)
        print_split_totals();
}

// The active hand is done: on to the next one, or the dealer.
static void next_hand(void) {
    if (hand_count == 2 && active == 0) {
        active = 1;
        layout_hand(0);
        layout_hand(1);
        // A split hand gets its second card when its turn comes.
        deal_player(1, 4);
        wait = 8;
        step = 1; // checked once it lands (split aces stand at once)
        print_split_totals();
        return;
    }
    if (hand_count == 2) {
        state = DEALER; // so the active hand drops back into line
        layout_hand(1);
    }
    start_dealer();
}

// After any card to the active hand: 21 stands by itself, over 21 busts.
static void check_active_hand(void) {
    int total = hand_total(&hands[active], 0);
    int last = hand_ids[active][hands[active].count - 1];
    if (total > 21) {
        psg_play(SND_BUST);
        table_nudge(last, -14);
        if (hand_count == 1)
            banner_show("BUST", 150, BANNER_Y, LETTERS_RED);
        next_hand();
    } else if (total == 21 || doubled[active] ||
               (hand_count == 2 && CARD_RANK(hands[active].cards[0]) == RANK_ACE)) {
        // 21, a double (one card only) or split aces (one card each).
        if (total == 21)
            table_nudge(last, 8);
        next_hand();
    }
    if (hand_count == 2 && state == PLAYING)
        print_split_totals();
}

static void choose_action(int action) {
    switch (action) {
    case 0: // hit
        deal_player(active, 0);
        wait = 4;
        break;
    case 1: // stand
        next_hand();
        break;
    case 2: // double: the bet doubles, one more card
        bankroll.bank -= (u32)bets[active];
        chips_fly(bets[active], BANK_X + 16, BANK_Y + 8, STACK_X, STACK_Y - 12);
        stack_amount += bets[active];
        print_bet(stack_amount);
        bets[active] *= 2;
        doubled[active] = true;
        chips_set(stack_amount, STACK_X, STACK_Y, CHIPS_LANDING);
        banner_show("DOUBLE", 150, BANNER_Y, LETTERS_SILVER);
        deal_player(active, 6);
        wait = 4;
        break;
    case 3: // split: two hands, each with the bet
        bankroll.bank -= (u32)bets[0];
        chips_fly(bets[0], BANK_X + 16, BANK_Y + 8, STACK_X, STACK_Y - 12);
        bets[1] = bets[0];
        hand_count = 2;
        hands[1].cards[0] = hands[0].cards[1];
        hands[1].count = 1;
        hand_ids[1][0] = hand_ids[0][1];
        hands[0].count = 1;
        stack_amount += bets[0];
        print_bet(stack_amount);
        chips_set(stack_amount, STACK_X, STACK_Y, CHIPS_LANDING);
        layout_hand(1);
        text_print_line(0, 11, "HANDS");
        deal_player(0, 12); // the first hand's second card now
        wait = 4;
        print_split_totals();
        break;
    }
}

static void update_playing(void) {
    u16 enabled = enabled_actions();
    if (button_pressed(BUTTON_RIGHT)) {
        selected = (selected + 1) % 4;
        psg_play(SND_MOVE);
    } else if (button_pressed(BUTTON_LEFT)) {
        selected = (selected + 3) % 4;
        psg_play(SND_MOVE);
    }
    int action = -1;
    if (button_pressed(BUTTON_A)) {
        if (enabled & (1u << selected)) {
            action = selected;
        } else {
            psg_play(SND_NOPE);
            table_nudge(hand_ids[active][hands[active].count - 1], 6);
        }
    } else if (button_pressed(BUTTON_B)) {
        action = 1; // B stands, from anywhere
        selected = 1;
    }
    if (action < 0)
        return;
    pressed = 8;
    psg_play(SND_PRESS);
    choose_action(action);
    if (action == 0 || action == 2 || action == 3) {
        // check after the card lands (next step)
        step = 1;
    }
    if (!(enabled_actions() & (1u << selected)))
        selected = 1;
}

// The middle of hand h on screen.
static int hand_center(int h) {
    int x0, x1, y;
    player_card_pos(h, 0, &x0, &y);
    player_card_pos(h, hands[h].count - 1, &x1, &y);
    return (x0 + x1 + CARD_W) / 2;
}

static void settle_message(int h, const char* word, u16 color) {
    int x = hand_count == 1 ? 150 : h == 0 ? 100 : 186;
    banner_show(word, x, BANNER_Y, color);
}

// Pays hand h and shows its result. Returns the frames to wait after.
static int settle(int h) {
    bool dealer_bj = hand_blackjack(&dealer);
    bool player_bj = hand_count == 1 && hand_blackjack(&hands[h]);
    int player = hand_total(&hands[h], 0), dealer_total = hand_total(&dealer, 0);
    int bet = bets[h];
    int win; // what comes back to the bank: 0, the bet (push) or more
    const char* word;
    u16 color;
    if (player > 21) {
        win = 0;
        word = "BUST";
        color = LETTERS_RED;
    } else if (player_bj && !dealer_bj) {
        win = bet + bet * 3 / 2;
        word = "BLACKJACK!";
        color = LETTERS_GOLD;
    } else if (dealer_bj && !player_bj) {
        win = 0;
        word = "LOSE";
        color = LETTERS_RED;
    } else if (dealer_total > 21 || player > dealer_total) {
        win = 2 * bet;
        word = "WIN!";
        color = LETTERS_GOLD;
    } else if (player == dealer_total) {
        win = bet;
        word = "PUSH";
        color = LETTERS_SILVER;
    } else {
        win = 0;
        word = "LOSE";
        color = LETTERS_RED;
    }
    int hx = hand_center(h);
    if (h == 0)
        banner_clear(); // DOUBLE, BUST or DEALER BUST make way
    settle_message(h, word, color);
    bool won = win > bet;
    for (int i = 0; i < hands[h].count; i++) {
        table_glow(hand_ids[h][i], won);
        if (won)
            table_nudge(hand_ids[h][i], (i % 2) ? 7 : -7);
    }
    if (won) {
        bankroll.wins++;
        sparks_burst(hx, PLAYER_Y + 20, player_bj ? 10 : 8, false);
        if (player_bj) {
            round_has_blackjack = true;
            sparks_burst(hx, BANNER_Y, 24, true);
        }
        psg_play(player_bj ? SND_BLACKJACK : SND_WIN);
        // Winnings come over from the dealer, then everything to the bank.
        chips_fly(win - bet, DEALER_CHIPS_X, DEALER_CHIPS_Y, STACK_X, STACK_Y - 12);
        pop_number(win - bet, hx, PLAYER_Y + 30);
    } else if (win == bet) {
        psg_play(SND_PUSH);
    } else {
        psg_play(SND_LOSE);
        chips_fly(bet, STACK_X, STACK_Y - 12, DEALER_CHIPS_X, DEALER_CHIPS_Y);
        pop_number(-bet, hx, PLAYER_Y + 30);
    }
    stack_amount += win - bet;
    chips_set(stack_amount, STACK_X, STACK_Y, CHIPS_LANDING);
    return won ? 70 : 50;
}

static void update_round(void) {
    if (wait > 0) {
        wait--;
        return;
    }
    if (table_busy())
        return;
    switch (state) {
    case DEALING:
        // Player, dealer, player, dealer's hole card (face down).
        if (step == 0) {
            banner_clear();
            deal_player(0, 0);
            deal_dealer(true, DEAL_GAP);
            deal_player(0, DEAL_GAP * 2);
            deal_dealer(false, DEAL_GAP * 3);
            step = 1;
        } else {
            state = PEEK;
            step = 0;
        }
        break;
    case PEEK:
        if (step == 0 && dealer_upcard_peeks()) {
            table_lift(dealer_ids[1], 5);
            table_nudge(dealer_ids[1], 4);
            wait = 30;
            step = 1;
        } else if (step <= 1) {
            table_lift(dealer_ids[1], 0);
            if (hand_blackjack(&dealer) || hand_blackjack(&hands[0])) {
                // A natural on either side ends the round at once.
                if (hand_blackjack(&hands[0]))
                    table_nudge(hand_ids[0][1], 10);
                state = DEALER;
                step = 1; // straight to the reveal, no drawing
            } else {
                state = PLAYING;
                step = 0;
                selected = 0;
            }
        }
        break;
    case PLAYING:
        if (step == 1) { // a card just landed on the active hand
            step = 0;
            check_active_hand();
        }
        break;
    case DEALER:
        if (step == 0 || step == 1) {
            bool reveal_only = step == 1;
            if (!hole_shown) {
                hole_shown = true;
                table_flip(dealer_ids[1]);
                wait = 16;
            }
            bool all_bust = true;
            for (int h = 0; h < hand_count; h++)
                all_bust &= hand_total(&hands[h], 0) > 21;
            step = reveal_only || all_bust ? 3 : 2;
        } else if (step == 2) {
            // The dealer stands on every 17, soft ones too.
            if (hand_total(&dealer, 0) < 17) {
                deal_dealer(true, 0);
                wait = 12;
            } else {
                step = 3;
            }
        } else {
            if (hand_total(&dealer, 0) > 21) {
                banner_clear();
                banner_show("DEALER BUST", 150, BANNER_Y, LETTERS_GOLD);
                for (int i = 0; i < dealer.count; i++)
                    table_nudge(dealer_ids[i], (i % 2) ? -9 : 9);
                psg_play(SND_BUST);
                wait = 40;
            } else {
                wait = 12;
            }
            state = SETTLE;
            settle_hand = 0;
        }
        break;
    case SETTLE:
        if (settle_hand < hand_count) {
            wait = settle(settle_hand++);
        } else if (settle_hand == hand_count) {
            // Everything that came back goes to the bank.
            settle_hand++;
            if (stack_amount > 0)
                chips_fly(stack_amount, STACK_X, STACK_Y - 12, BANK_X + 16, BANK_Y + 8);
            bankroll.bank += (u32)stack_amount; // counts up on screen
            stack_amount = 0;
            chips_set(0, STACK_X, STACK_Y, 0);
            bankroll.hands++;
            if (round_has_blackjack)
                bankroll.blackjacks++;
            bank_save();
            wait = 70;
        } else {
            table_discard_all();
            start_betting();
        }
        break;
    default:
        break;
    }
}

// --- Title -------------------------------------------------------------------

static void start_game(void) {
    // random_entropy() depends only on the buttons pressed so far and when:
    // the same input plays the same game on the GBA and the web.
    random_seed(random_entropy());
    shoe_shuffle();
    psg_play(SND_START);
    fade_to(show_table);
}

static void update_title(void) {
    // For demos: hold L and R and press SELECT to reset the bankroll.
    if (button_down(BUTTON_L) && button_down(BUTTON_R) && button_pressed(BUTTON_SELECT)) {
        bank_reset();
        shown_bank = (int)bankroll.bank;
        psg_play(SND_RESET);
        text_print_centered(15, text_format("BANK %u   BEST %u", bankroll.bank, bankroll.best));
        text_print_centered(13, "BANKROLL RESET");
        wait = 90;
        return;
    }
    if (button_pressed(BUTTON_START)) {
        start_game();
        return;
    }
    if (wait > 0 && --wait == 0)
        text_print_line(0, 13, "");
    if (wait == 0) {
        if ((frame_count() / 30) % 2)
            text_print_centered(13, "PRESS START");
        else
            text_print_line(0, 13, "");
    }
    // The fan sways gently around its resting angles.
    int sway = fx_sin((u16)(frame_count() * 500)) * 3 / 256;
    for (int i = 0; i < 3; i++)
        table_set_angle(title_ids[i], (i - 1) * 10 + (i == 1 ? -sway : sway));
    if (bank_save_failed())
        text_print_centered(19, "(COULD NOT SAVE)");
}

// --- Frame -------------------------------------------------------------------

void game_init(void) {
    screen_set_brightness(SCREEN_BRIGHTNESS_MIN); // black while building the art
    art_build();
    psg_table_set(sound_table, SOUND_COUNT);
    sprite_table_set(sprite_table, SPRITE_COUNT);
    sprite_group_load(&sprite_group);
    text_set_shadow(true);
    button_repeat_set(REPEAT_DELAY, REPEAT_RATE); // the bet: a little quicker than the default
    bank_load();
    shown_bank = (int)bankroll.bank;
    show_title();
}

void game_frame(void) {
    drift_background();
    bool fading = update_fade();
    if (!fading) {
        switch (state) {
        case TITLE:
            update_title();
            break;
        case BETTING:
            update_betting();
            break;
        default:
            if (state == PLAYING && step == 0 && wait == 0 && !table_busy())
                update_playing();
            else
                update_round();
            break;
        }
    }
    table_update();
    if (state != TITLE)
        update_bank_count();
    if (pressed > 0)
        pressed--;

    // Drawing, front to back (the first sprite drawn is in front).
    if (state != TITLE) {
        draw_hud();
        if (state == BETTING)
            buttons_draw(deal_button, 1, 0, 1, pressed);
        else if (state == PLAYING)
            buttons_draw(actions, 4, selected, enabled_actions(), pressed);
    }
    table_draw();
    if (state != TITLE)
        draw_shoe();
}
