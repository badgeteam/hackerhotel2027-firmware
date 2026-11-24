// 4 in a row: drop discs into seven columns, six high; four of yours in a
// line (across, up or diagonal) wins. C / D aim, B drops. Against another
// badge over infrared: open 4 in a row on both and point them at each
// other, and they pair up. Drop a disc before that, and you play against
// this badge. After a game, B starts the next one.
//
// Your discs are bright, the other side's dim. The arms are green on your
// turn and orange on theirs, and a blue wave runs out while the badge looks
// for another one.
//
// Infrared: packet type 'F', data [game, move, column, flags]. `game` is
// a random number that keeps two pairs of players apart. A badge looking
// for a partner sends move 0 with FL_ANSWER about every second; a badge
// that is looking too answers and joins, and the one that asked moves
// first (when both ask, the higher number wins). The one that joined sets
// FL_SECOND in everything it sends. After that each move goes out as soon
// as it is made, and again every 0.8 s until the other badge answers. An
// answer is our own last move, so whichever packet arrives, both badges
// end up with the same board. While it is the other badge's turn, ours
// asks every 3 s whether it is still there; 20 s without an answer ends
// the game. As in the previous firmware's 4 in a row.
#include "hhapp.h"

#define W 7
#define H 6
#define X0 2        // column c is grid column X0 + 2c
#define Y0 5        // row r (0 = top) is grid row Y0 + r
#define CURSOR_Y 3
#define PACKET 'F'
#define FL_ANSWER 1  // please answer: we have not heard you got this
#define FL_SECOND 2  // the sender moves second
#define WIN_BIT 4    // the cell is part of the winning line
#define SEEK_MS 1000      // looking: ask this often, plus up to 0.6 s
#define RESEND_MS 800     // an unanswered move again, plus up to 0.4 s
#define PING_MS 3000      // their turn: ask whether they are still there
#define LOST_MS 20000     // no answer for this long: the link is gone
#define THINK_MS 500      // the badge answers your move after this
#define END_MS 3000       // the result shows this long before B starts a new game

enum { SEEK, LOCAL, LINK };

static const hh_api_t* hh;
static uint8_t cell[H][W];  // 0, or the player (1 moves first, 2) | WIN_BIT
static uint8_t moves[W * H];
static uint8_t n;           // moves made
static uint8_t last_r, last_c;
static uint8_t cursor;
static uint8_t me;          // we are player me + 1
static uint8_t mode;
static uint8_t game;        // the infrared game's number
static uint8_t heard;       // the other badge has our moves up to this one
static int32_t answer_in;   // ms until we answer the other badge; < 0: no
static uint8_t winner;      // 0, the player, or 3 for a draw
static uint32_t end_ms, resend_ms, quiet_ms, since_ms;
static bool lost;           // the link went

static int row_free(int c) {
    for (int r = H - 1; r >= 0; r--)
        if (!cell[r][c]) return r;
    return -1;
}

static uint8_t owner(int r, int c) { return r >= 0 && r < H && c >= 0 && c < W ? cell[r][c] & 3 : 0; }

static const int8_t DIRS[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};

// Discs of player p through (r, c) in direction d, (r, c) counted as p's;
// mark: also mark them as the winning line
static int line(int r, int c, int d, uint8_t p, bool mark) {
    int count = 1;
    for (int s = -1; s <= 1; s += 2)
        for (int k = 1; owner(r + s * k * DIRS[d][0], c + s * k * DIRS[d][1]) == p; k++) {
            count++;
            if (mark) cell[r + s * k * DIRS[d][0]][c + s * k * DIRS[d][1]] |= WIN_BIT;
        }
    return count;
}

static bool wins(int r, int c, uint8_t p) {
    for (int d = 0; d < 4; d++)
        if (line(r, c, d, p, false) >= 4) return true;
    return false;
}

static bool wins_in(int c, uint8_t p) {
    int r = row_free(c);
    return r >= 0 && wins(r, c, p);
}

// The badge's move for player p: win, block, keep to the middle, and do
// not set up the other side's win on top
static int ai(uint8_t p) {
    for (int q = 0; q < 2; q++)
        for (int c = 0; c < W; c++)
            if (wins_in(c, q ? 3 - p : p)) return c;
    int best = -1, best_v = -1000;
    for (int c = 0; c < W; c++) {
        int r = row_free(c);
        if (r < 0) continue;
        int v = (3 - (c > 3 ? c - 3 : 3 - c)) * 4 + (int)(hh->random() % 4);
        cell[r][c] = p;
        if (r > 0 && wins(r - 1, c, 3 - p)) v -= 50;
        cell[r][c] = 0;
        if (v > best_v) best = c, best_v = v;
    }
    return best;
}

static void print_result(void) {
    char text[48] = "four: ";
    const char* what = winner == 3 ? "draw" : winner == me + 1 ? "you win" : "you lose";
    int k = 6;
    while (*what) text[k++] = *what++;
    const char* m = ", moves ";
    while (*m) text[k++] = *m++;
    for (int i = 0; i < n && k < (int)sizeof text - 2; i++) text[k++] = (char)('1' + moves[i]);
    text[k++] = '\n';
    text[k] = 0;
    hh->print(text);
}

// The next move, in column c (it fits)
static void drop(int c) {
    uint8_t p = (uint8_t)(n % 2 + 1);
    int r = row_free(c);
    if (r < 0) return;
    cell[r][c] = p;
    moves[n++] = (uint8_t)c;
    last_r = (uint8_t)r;
    last_c = (uint8_t)c;
    since_ms = 0;
    bool won = false;
    for (int d = 0; d < 4; d++)
        if (line(r, c, d, p, false) >= 4) {
            line(r, c, d, p, true);
            won = true;
        }
    if (won) {
        cell[r][c] |= WIN_BIT;
        winner = p;
    } else if (n == W * H) {
        winner = 3;
    }
    if (winner) {
        end_ms = 0;
        print_result();
    }
}

static bool my_turn(void) { return n % 2 == me; }

// Our state to the other badge: false if the transmitter is busy (we try
// again next tick)
static bool send(bool ask) {
    uint8_t d[4] = {game, n, n ? moves[n - 1] : 0, (uint8_t)((ask ? FL_ANSWER : 0) | (me ? FL_SECOND : 0))};
    return hh->ir_send_packet(PACKET, d, sizeof d);
}

static void new_game(void) {
    for (int r = 0; r < H; r++)
        for (int c = 0; c < W; c++) cell[r][c] = 0;
    n = 0;
    cursor = 3;
    me = 0;
    mode = SEEK;
    game = (uint8_t)(1 + hh->random() % 255);
    heard = 0;
    answer_in = -1;
    winner = 0;
    lost = false;
    resend_ms = 0;  // ask at once
    quiet_ms = since_ms = 0;
}

static void receive(const hh_ir_packet_t* pk) {
    if (pk->type != PACKET || pk->length != 4) return;
    uint8_t g = pk->data[0], m = pk->data[1], c = pk->data[2], flags = pk->data[3];
    bool asks = flags & FL_ANSWER, second = flags & FL_SECOND;
    bool invite = m == 0 && asks && !second;
    if (mode == SEEK) {
        if (invite && g > game) {  // join theirs; they move first
            game = g;
            me = 1;
        } else if (m == 0 && g == game && second) {
            me = 0;  // they joined ours
        } else {
            if (invite && g == game) game = (uint8_t)(1 + hh->random() % 255);  // the same number: pick another
            return;
        }
        mode = LINK;
        resend_ms = PING_MS;
        hh->print(me ? "four: linked, they start\n" : "four: linked, you start\n");
    } else if (mode != LINK || g != game || second == (me != 0)) {
        return;  // not our game, or not the other player
    }
    quiet_ms = 0;
    if (m == n + 1 && !winner && !my_turn() && c < W && row_free(c) >= 0) drop(c);
    if (m <= n && m > heard) heard = m;
    if (asks && answer_in < 0) answer_in = 100 + (int32_t)(hh->random() % 170);  // let their receiver settle
}

static void tick(uint32_t elapsed_ms) {
    uint8_t pressed = hh->pressed();
    since_ms += elapsed_ms;

    hh_ir_packet_t pk;
    while (hh->ir_receive_packet(&pk)) receive(&pk);

    if (answer_in >= 0) {
        answer_in -= (int32_t)elapsed_ms;
        if (answer_in < 0 && !send(n > heard)) answer_in = 0;  // busy: next tick
    }
    if (mode == SEEK) {
        if (resend_ms <= elapsed_ms) {
            if (send(true)) resend_ms = SEEK_MS + hh->random() % 600;
        } else {
            resend_ms -= elapsed_ms;
        }
    }
    // They have not got our last move, or it is their turn: keep asking
    if (mode == LINK && !lost && (n > heard || (!my_turn() && !winner))) {
        quiet_ms += elapsed_ms;
        if (quiet_ms > LOST_MS) {
            lost = true;
            end_ms = 0;
            hh->print("four: link lost\n");
        } else if (resend_ms <= elapsed_ms) {
            if (send(true)) resend_ms = (n > heard ? RESEND_MS : PING_MS) + hh->random() % 400;
        } else {
            resend_ms -= elapsed_ms;
        }
    }

    if (winner || lost) {
        end_ms += elapsed_ms;
        if (end_ms >= END_MS && (pressed & HH_BTN_B)) new_game();
    } else {
        if ((pressed & HH_BTN_C) && cursor > 0) cursor--;
        if ((pressed & HH_BTN_D) && cursor < W - 1) cursor++;
        if (mode == LOCAL && n % 2 == 1) {  // the badge's turn
            if (since_ms >= THINK_MS) {
                int c = ai(2);
                if (c >= 0) drop(c);
            }
        } else if ((pressed & HH_BTN_B) && my_turn() && row_free(cursor) >= 0) {
            if (mode == SEEK) mode = LOCAL;
            drop(cursor);
            if (mode == LINK) {
                quiet_ms = 0;
                resend_ms = 0;  // send it now
            }
        }
    }

    // Draw
    uint32_t t = hh->millis();
    hh->clear();
    for (int x = 1; x < HH_GRID_W - 1; x++) hh->set(hh->grid[Y0 + H][x], 20);  // the floor
    for (int r = 0; r < H; r++)
        for (int c = 0; c < W; c++) {
            uint8_t v = cell[r][c];
            if (!v) continue;
            uint8_t b = (v & 3) == me + 1 ? 255 : 60;
            if ((v & WIN_BIT) && t / 160 % 2) b = 0;
            else if (n && r == last_r && c == last_c && since_ms < 1000 && since_ms / 130 % 2) b = 0;  // the last disc blinks
            hh->set(hh->grid[Y0 + r][X0 + 2 * c], b);
        }
    if (!winner && !lost && my_turn() && !(mode == LOCAL && n % 2))
        hh->set(hh->grid[CURSOR_Y][X0 + 2 * cursor], t / 270 % 3 ? 255 : 90);

    uint8_t cr, cg, cb;
    if (winner == 3 || lost) {
        cr = cg = cb = 200;
    } else if (winner) {
        bool won = winner == me + 1;
        cr = won ? 0 : 255, cg = won ? 255 : 20, cb = won ? 60 : 0;
    } else if (mode == SEEK) {  // looking for a badge: a blue wave runs out
        for (int i = 0; i < HH_RGB_COUNT; i++)
            if (t / 100 % 9 == hh->rgb[i].rank) hh->set(hh->rgb[i].b, 255), hh->set(hh->rgb[i].g, 80);
        return;
    } else if (my_turn()) {
        cr = 0, cg = 255, cb = 60;
    } else {
        cr = 255, cg = 90, cb = 0;
    }
    uint32_t ph = t / 8 % 256;
    uint32_t pulse = 120 + (ph < 128 ? ph : 255 - ph);  // 120 .. 247
    for (int i = 0; i < HH_RGB_COUNT; i++) {
        hh->set_rgb(i, (uint8_t)(cr * pulse / 255), (uint8_t)(cg * pulse / 255), (uint8_t)(cb * pulse / 255));
    }
}

static void init(const hh_api_t* api) {
    hh = api;
    new_game();
}

HH_APP(init, tick);
