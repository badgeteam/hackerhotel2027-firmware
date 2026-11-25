// apps/bottle on the PC (mock_hh.h): C adds players, spread over the 10
// places (arms and the outline's dips), B takes one away; D spins the
// bottle, which slows down smoothly and stops well inside a place; every
// place comes up about as often; a player's place wins, an empty one is a
// miss; with fewer than 2 players it asks for more ("C+").
#include "mock_hh.h"
#include "../../apps/bottle/bottle.c"

static void run(uint32_t ms, uint8_t pressed) {
    mock_ms += ms;
    mock_pressed_now = pressed;
    tick(ms);
    mock_tick_end();
    mock_pressed_now = 0;
}

// The LEDs place p lights at radius r, as a bit per slot
static void place_leds(uint8_t p, uint8_t r, uint8_t lit[HH_SLOTS]) {
    m_clear();
    light(p, r, 1, 1, 1, 1);
    for (int s = 0; s < HH_SLOTS; s++) lit[s] = mock_leds[s] != 0;
}

// Is place p lit now (any of its LEDs at the calm radius)
static bool lit_now(uint8_t p) {
    static uint8_t mask[PLACES][HH_SLOTS];
    static bool done;
    if (!done) {
        uint8_t keep[HH_SLOTS];
        memcpy(keep, mock_leds, sizeof keep);
        for (uint8_t q = 0; q < PLACES; q++) place_leds(q, CALM_R, mask[q]);
        memcpy(mock_leds, keep, sizeof keep);
        done = true;
    }
    for (int s = 0; s < HH_SLOTS; s++)
        if (mask[p][s] && mock_leds[s]) return true;
    return false;
}

static int gap(int a, int b) { return (b - a + PLACES) % PLACES; }

// Spin once with big steps, to the end
static void spin_to_end(void) {
    run(10, HH_BTN_D);
    while (state == SPIN) run(1000, 0);
}

// How many of place p's slots are bright (a win's amber)
static int bright(uint8_t p) {
    uint8_t m[HH_SLOTS], keep[HH_SLOTS];
    memcpy(keep, mock_leds, sizeof keep);
    place_leds(p, 255, m);
    memcpy(mock_leds, keep, sizeof keep);
    int n = 0;
    for (int s = 0; s < HH_SLOTS; s++) n += m[s] && mock_leds[s] > 200;
    return n;
}

// The brightest grid pixel
static uint8_t grid_max(void) {
    uint8_t v = 0;
    for (int y = 0; y < HH_GRID_H; y++)
        for (int x = 0; x < HH_GRID_W; x++) {
            uint16_t s = layout_grid[y][x];
            if (s != HH_NONE && mock_leds[s] > v) v = mock_leds[s];
        }
    return v;
}

int main(void) {
    (void)mock_rgb;
    init(&mock_api);
    CHECK(players == 0 && state == IDLE, "it starts with no players");

    // The order: every place once, never next to the one before, spread
    int seen = 0, bad = 0;
    for (int i = 0; i < PLACES; i++) seen |= 1 << ORDER[i];
    for (int i = 1; i < PLACES; i++) bad += gap(ORDER[i - 1], ORDER[i]) == 1 || gap(ORDER[i], ORDER[i - 1]) == 1;
    CHECK(seen == (1 << PLACES) - 1, "the order has every place once");
    CHECK(bad == 0, "a new player's place is never next to the one before (%d)", bad);
    CHECK(ORDER[0] == 0 && ORDER[1] == 5, "the top arm first, then the dip opposite");
    for (int n = 2; n <= PLACES; n++) {
        int widest = 0;
        for (int p = 0; p < PLACES; p++) {
            bool in = false;
            for (int i = 0; i < n; i++) in = in || ORDER[i] == p;
            if (!in) continue;
            int g = PLACES;
            for (int i = 0; i < n; i++)
                if (ORDER[i] != p && gap(p, ORDER[i]) < g) g = gap(p, ORDER[i]);
            if (g > widest) widest = g;
        }
        CHECK(widest <= (PLACES + n - 1) / n + 1, "%d players: no gap wider than %d places (%d)", n, (PLACES + n - 1) / n + 1, widest);
    }

    // Each place has LEDs of its own: at least 3 when taken, more as it grows
    static uint8_t a[HH_SLOTS], b[HH_SLOTS], full[HH_SLOTS];
    for (uint8_t p = 0; p < PLACES; p++) {
        place_leds(p, CALM_R, a);
        place_leds(p, 255, full);
        int n = 0, nf = 0, shared = 0;
        for (int s = 0; s < HH_SLOTS; s++) n += a[s], nf += full[s];
        for (uint8_t q = 0; q < PLACES; q++) {
            if (q == p) continue;
            place_leds(q, 255, b);
            for (int s = 0; s < HH_SLOTS; s++) shared += full[s] && b[s];
        }
        int min = p & 1 ? 3 : 3 * 3;  // an RGB LED is 3 slots
        CHECK(n >= min, "place %d lights %d slots when taken", p, n);
        CHECK(nf > n, "place %d lights more when it wins (%d)", p, nf);
        CHECK(shared == 0, "place %d shares no LED with another (%d)", p, shared);
        // its LEDs point its way
        int off = 0;
        for (int s = 0; s < HH_SLOTS; s++)
            if (a[s]) {
                int d = (int8_t)(layout_polar[s][1] - (uint8_t)(p * 128 / 5));
                if (d > off) off = d;
                if (-d > off) off = -d;
            }
        CHECK(off < 26, "place %d's LEDs are within its own sector (%d)", p, off);
    }

    // C adds players, lit in order; B takes the last one away
    init(&mock_api);
    for (int n = 1; n <= PLACES + 2; n++) {
        run(10, HH_BTN_C);
        run(10, 0);
        int want = n > PLACES ? PLACES : n, wrong = 0;
        for (int i = 0; i < PLACES; i++) wrong += lit_now(ORDER[i]) != (i < want);
        CHECK(players == want && wrong == 0, "after %d presses of C: %d players lit (%d wrong)", n, want, wrong);
    }
    CHECK(mock_text[0] == '1' && mock_text[1] == '0', "the grid shows the number of players (%s)", mock_text);
    run(10, HH_BTN_B);
    run(10, 0);
    CHECK(players == 9 && !lit_now(ORDER[9]) && lit_now(ORDER[8]), "B takes the last player away");
    run(10, HH_BTN_A);
    CHECK(players == 9 && state == IDLE, "A does nothing (it is the menu's)");
    for (int i = 0; i < 12; i++) run(10, HH_BTN_B);
    CHECK(players == 0, "B stops at no players");

    // The spin: fast, then smoothly slower, at least 5 turns, stops on a place
    players = 3;
    run(COUNT_MS, 0);
    uint16_t start = angle;
    run(5, HH_BTN_D);
    CHECK(state == SPIN, "D spins the bottle");
    CHECK(saved == 3 && mock_saves == 1, "the number of players is saved when it spins");
    uint32_t turned = (uint16_t)(angle - start), last_step = 65536, rises = 0, first = 0;
    for (int i = 0; state == SPIN && i < 4000; i++) {
        uint16_t was = angle;
        uint8_t was_players = players;
        run(5, i % 7 == 0 ? HH_BTN_C : i % 7 == 2 ? HH_BTN_B : i % 7 == 3 ? HH_BTN_D : 0);
        CHECK(players == was_players, "no players change while it spins");
        uint32_t step = (uint16_t)(angle - was);
        if (!first) first = step;
        rises += step > last_step + last_step / 20 + 2;  // rounding
        last_step = step;
        turned += step;
    }
    CHECK(first * 200 > 2 * 65536, "it starts fast: %u/65536 turn in 5 ms", first);
    CHECK(rises == 0, "and only ever slows down (%u faster steps)", rises);
    CHECK(last_step < 20, "to a stop: the last step %u", last_step);
    CHECK(turned >= 5 * 65536u && turned < 6 * 65536u, "%u turns", turned / 65536);
    CHECK(dur >= 4000 && dur <= 5000, "in %u ms", dur);
    spin_to_end();
    CHECK(mock_saves == 1, "a spin with the same players saves nothing (%u saves)", mock_saves);
    run(10, HH_BTN_C);
    spin_to_end();
    CHECK(saved == 4 && mock_saves == 2, "one more player: saved again (%u saves)", mock_saves);

    // Fair: every place about as often, the bottle well inside it
    players = PLACES;  // every spin wins
    static int hits[PLACES];
    int inside = 0, wins = 0, spins = 20000, off_lo = 0, off_hi = 0;
    for (int i = 0; i < spins; i++) {
        spin_to_end();
        hits[place]++;
        int off = (int16_t)(angle - (uint16_t)((uint32_t)place * 65536 / PLACES));
        if (off < off_lo) off_lo = off;
        if (off > off_hi) off_hi = off;
        if (off < 0) off = -off;
        inside += off <= SECTOR / 3 + 1;
        wins += state == WIN;
    }
    int lo = spins, hi = 0;
    for (int p = 0; p < PLACES; p++) {
        if (hits[p] < lo) lo = hits[p];
        if (hits[p] > hi) hi = hits[p];
    }
    CHECK(lo > spins / PLACES * 9 / 10 && hi < spins / PLACES * 11 / 10, "each place within 10%% of %d: %d .. %d", spins / PLACES, lo, hi);
    CHECK(inside == spins, "the bottle stops in the middle 2/3 of a place (%d of %d)", inside, spins);
    CHECK(off_lo < -SECTOR / 4 && off_hi > SECTOR / 4, "anywhere in that middle part (%d .. %d)", off_lo, off_hi);

    // The winner's place never dims from the last spinning frame: it only grows
    int drops = 0, checked = 0;
    for (int i = 0; i < 30; i++) {
        run(10, HH_BTN_D);
        int last = 0;
        while (state == SPIN) {
            last = bright(place_of(angle));
            run(5, 0);
        }
        uint8_t p = place;
        for (int k = 0; k < 8; k++) {
            drops += bright(p) < last;
            run(25, 0);
        }
        checked += last > 0;
    }
    CHECK(checked == 30 && drops == 0, "the win lights outward from what is lit (%d drops in %d)", drops, checked);
    CHECK(wins == spins, "with all 10 places taken every spin wins (%d)", wins);

    // Wins only on a player's place, misses only on an empty one
    players = 3;
    int win_arm = 0, win_dip = 0, misses = 0, wrong = 0;
    for (int i = 0; i < 400; i++) {
        run(10, i % 2 ? HH_BTN_C : HH_BTN_B);  // keep 3 players, leave WIN
        spin_to_end();
        if (state == WIN) {
            wrong += !taken(place);
            if (place & 1) win_dip++;
            else win_arm++;
        } else {
            wrong += state != MISS || taken(place);
            misses++;
        }
    }
    CHECK(wrong == 0, "a player's place wins, an empty one misses (%d wrong)", wrong);
    CHECK(misses > 200 && win_arm > 20 && win_dip > 20, "3 players: %d misses, %d arm wins, %d dip wins", misses, win_arm, win_dip);

    // A miss blinks the empty place, then D again
    do spin_to_end();
    while (state != MISS);
    uint8_t p = place;
    bool blinked = false;
    uint8_t peak = 0, grid_hi = 0, grid_lo = 255;
    uint8_t pm[HH_SLOTS];
    place_leds(p, CALM_R, pm);
    for (int i = 0; i < MISS_MS / 25; i++) {
        run(25, 0);
        blinked = blinked || lit_now(p);
        for (int s = 0; s < HH_SLOTS; s++)
            if (pm[s] && mock_leds[s] > peak) peak = mock_leds[s];
        uint8_t g = grid_max();
        if (g > grid_hi) grid_hi = g;
        if (g < grid_lo) grid_lo = g;
    }
    run(25, 0);
    CHECK(blinked && state == IDLE && !lit_now(p), "an empty place blinks, then it waits for D");
    CHECK(peak >= 120 && peak < 200, "clearly, but less than a win (%d)", peak);
    CHECK(grid_hi == 255 && grid_lo == 0, "and the bottle blinks with it (%d .. %d)", grid_lo, grid_hi);

    // A win on an arm: its LEDs light from the centre outward, then pulse
    do spin_to_end();
    while (!(state == WIN && !(place & 1)));
    int arm = place / 2, inner = arm * 7, tip = arm * 7 + 6;
    uint8_t c0[3], c6[3];
    run(50, 0);
    mock_rgb(inner, c0), mock_rgb(tip, c6);
    CHECK(c0[0] > 200 && c6[0] == 0, "the winning arm lights its inner LED first");
    run(WIN_GROW_MS, 0);
    mock_rgb(tip, c6);
    CHECK(c6[0] > 100, "then out to the tip");
    uint8_t lowest = 255, highest = 0;
    for (int i = 0; i < 300; i++) {
        run(5, 0);
        mock_rgb(tip, c6);
        if (c6[0] < lowest) lowest = c6[0];
        if (c6[0] > highest) highest = c6[0];
    }
    CHECK(highest - lowest > 60, "and pulses (%d .. %d)", lowest, highest);
    // still lit to the tip after half a day without a button
    lowest = 255;
    for (t_ms = 42949000; t_ms < 42951000;) {
        run(25, 0);
        mock_rgb(tip, c6);
        if (c6[0] < lowest) lowest = c6[0];
    }
    CHECK(lowest >= 100, "and still pulses at the tip after 12 hours (%d)", lowest);
    run(10, HH_BTN_C);
    CHECK(state == IDLE && players == 4, "C after a win: a new round with one more player");

    // A win on a dip: the outline lights outward from the dip
    do spin_to_end();
    while (!(state == WIN && (place & 1)));
    uint8_t far_mask[HH_SLOTS];
    place_leds(place, 255, far_mask);
    run(WIN_GROW_MS / 3, 0);
    int early = 0, late = 0, lit_d = 0, dark_d = 255;
    for (int s = 0; s < HH_SLOTS; s++) {
        if (!far_mask[s]) continue;
        if (mock_leds[s] > 200) {
            early++;
            if (layout_polar[s][0] > lit_d) lit_d = layout_polar[s][0];
        } else if (layout_polar[s][0] < dark_d) {
            dark_d = layout_polar[s][0];
        }
    }
    run(WIN_GROW_MS, 0);
    for (int s = 0; s < HH_SLOTS; s++) late += far_mask[s] && mock_leds[s];
    CHECK(early >= 3 && lit_d < dark_d && late >= early + 4, "a winning dip lights outward (%d, out to %d, then %d)", early, lit_d, late);

    // Fewer than 2 players: it spins, then asks for players
    while (players > 1) run(10, HH_BTN_B);
    spin_to_end();
    CHECK(state == IDLE, "1 player: it spins, but nobody wins");
    mock_text[0] = 0;
    for (int i = 0; i < 100; i++) run(25, 0);
    CHECK(!strcmp(mock_text, "C+"), "and asks for players (%s)", mock_text);

    int was = players;
    init(&mock_api);
    CHECK(players == was, "the number of players is loaded again (%d)", players);
    return mock_report("bottle");
}
