// The LA04 decoder: per-file canonical Huffman tables, read once in
// LA04_Open(); after that each frame is table lookups, shifts and adds (no
// adaptive state). The format is in docs/ANIMATION_FORMAT.md, the encoder in
// webui/compression.js; the two must match bit for bit.
#include "la04.h"
#include <string.h>
#include "apps/hhapp.h"
#include "la04_tables.h"
#include "layout.h"

#define N 384   // positions: the grid, the outline, then RGB (layout_order)
#define NG 208  // grid positions
#define NE 71   // outline positions
#define NFAM 4  // mode families: each has its own contexts
#define NCTX 16
#define MAXL 12          // longest Huffman code
#define NTAB 16          // token tables at most
#define HOLD_SYMS 34     // hold bins (lengths 1..65535)
#define TMAX 32          // listed tuples at most
#define POOL (NTAB * 37 + (TMAX + 1 + HOLD_SYMS) + (TMAX + 1))

// The prediction modes
enum { M_Z, M_T, M_V, M_S, M_G, M_A, M_B, M_L, M_R, M_U, M_D, M_ROT, M_P, M_X };
// A mode index in the file -> the mode (low 4 bits) and its family (high
// bits); the outline rotations carry their amount in edge_rot
static const uint8_t grid_modes[16] = {M_Z, M_T, M_V | 1 << 4, M_S | 2 << 4, M_G | 3 << 4, M_L,
                                       M_R, M_U, M_D, M_P, M_X | 1 << 4};
static const uint8_t edge_modes[16] = {M_Z, M_T, M_V | 1 << 4, M_S | 2 << 4, M_G | 3 << 4, M_ROT, M_ROT,
                                       M_ROT, M_ROT, M_ROT, M_ROT, M_P, M_X | 1 << 4};
static const int8_t edge_rot[16] = {0, 0, 0, 0, 0, 1, -1, 2, -2, 3, -3};
static const uint8_t rgb_modes[16] = {M_Z, M_T, M_V | 1 << 4, M_S | 2 << 4, M_G | 3 << 4,
                                      M_A | 2 << 4, M_B | 3 << 4, M_P, M_X | 1 << 4};

typedef struct {
    uint16_t lim;  // the last left-justified 16-bit code value of this length
    int16_t base;  // the symbol of code c of this length is pool[base + c]
} hlen_t;
typedef struct {
    hlen_t L[MAXL + 1];  // per code length; L[0]: a table of one symbol (0 bits)
    uint8_t minl;        // the shortest code length
} htab_t;

// The big state, in the app RAM block
typedef struct {
    uint8_t buf[3][N] __attribute__((aligned(4)));  // this frame and the two before
    htab_t tab[NTAB + 2];                           // token tables, then frame tables 0 and 1
    uint8_t pool[POOL];
    uint8_t map[3 * NFAM * NCTX];  // context -> token table
    uint8_t ord[NG];               // the scan order of the last grid shift mode
    int8_t tt[256];                // the transition table: key -> delta to prev
} la04_ram_t;
_Static_assert(sizeof(la04_ram_t) <= HH_APP_RAM_SIZE, "LA04 state must fit the app RAM");

#ifdef LA04_RAM_IN_BSS  // host builds (tools/codec_test)
static la04_ram_t s_ram;
#define R (&s_ram)
#else
#define R ((la04_ram_t*)(uintptr_t)HH_APP_RAM_BASE)
#endif

static uint8_t *s_cur, *s_prev, *s_prev2;
static const uint8_t *s_d, *s_p, *s_end;
static uint32_t s_bb;            // bit buffer, the next bit in bit 31
static int s_bn;                 // valid bits in s_bb
static uint32_t s_tup, s_start;  // bit offsets: the tuple list, the first frame
static uint16_t s_nf, s_fi, s_hold;
static uint8_t s_nt, s_q, s_m, s_nres, s_eob, s_fctx, s_tbits;
static uint8_t s_deq[16];
static uint8_t s_ordm;               // the mode of R->ord
static uint8_t s_ts, s_tk, s_tkind;  // the transition table's key: see tt_key
static uint8_t s_act;                // activity contexts (else T/U)
static uint16_t s_thr[3];            // activity thresholds

// --- Bits (most significant first); past the end of the file they are 0 ---

static void refill(void) {
    while (s_bn <= 24) {
        s_bb |= (uint32_t)(s_p < s_end ? *s_p : 0) << (24 - s_bn);
        s_p++;
        s_bn += 8;
    }
}

static unsigned bits(unsigned n) {  // n <= 24
    refill();
    unsigned v = n ? s_bb >> (32 - n) : 0;
    s_bb <<= n;
    s_bn -= n;
    return v;
}

static void seek(uint32_t bit) {
    s_p = s_d + (bit >> 3);
    s_bb = 0;
    s_bn = 0;
    bits(bit & 7);
}

static uint32_t tell(void) { return (uint32_t)(s_p - s_d) * 8 - s_bn; }

static unsigned read_gamma(void) {  // Elias gamma: k zero bits, a one, the low k bits
    unsigned k = 0;
    while (!bits(1) && k < 16) k++;
    return (1u << k) + bits(k);
}

static unsigned nbits_for(unsigned n) {
    unsigned b = 0;
    while ((1u << b) < n) b++;
    return b;
}

// --- Huffman ---

static unsigned hdec(const htab_t* t) {
    refill();
    const hlen_t* e = &t->L[t->minl];
    uint32_t v = s_bb >> 16;
    while (v > e->lim) e++;
    unsigned l = (unsigned)(e - t->L);
    s_bb <<= l;
    s_bn -= (int)l;
    return R->pool[e->base + (v >> (16 - l))];
}

// Code lengths: the index e of the last used symbol, then events up to it
// against p (the last nonzero length, first 0): '00' p, '01' p + 1, '110'
// p - 1, '1110' p + 2, '1111' p - 2, '101' + 4 bits a new length, '100' +
// gamma(n) n unused symbols. Builds the table at pool + *pool.
static int read_table(htab_t* t, unsigned n, unsigned* pool) {
    uint8_t len[TMAX + 1 + HOLD_SYMS];
    unsigned cnt[MAXL + 1], e = bits(nbits_for(n)), l = 0, i, used = 0;
    if (e >= n) return -1;
    memset(len, 0, sizeof(len));
    memset(cnt, 0, sizeof(cnt));
    for (i = 0; i <= e;) {
        if (!bits(1)) {
            l += bits(1);
        } else if (!bits(1)) {
            if (!bits(1)) {
                i += read_gamma();
                continue;
            }
            l = bits(4);
        } else if (!bits(1)) {
            l--;
        } else {
            l += bits(1) ? -2u : 2u;
        }
        if (l - 1 >= MAXL) return -1;  // 1..MAXL
        len[i++] = (uint8_t)l;
        cnt[l]++, used++;
    }
    if (i != e + 1) return -1;
    if (*pool + used > POOL) return -1;
    unsigned code = 0, off = *pool, k = 0;
    t->minl = 0;
    t->L[0].lim = 0xFFFF;
    t->L[0].base = (int16_t)off;
    for (l = 1; l <= MAXL; l++) {
        if (cnt[l] && !t->minl && used > 1) t->minl = (uint8_t)l;
        t->L[l].base = (int16_t)(off - code);
        code += cnt[l];
        off += cnt[l];
        t->L[l].lim = (uint16_t)((code << (16 - l)) - 1);
        for (i = 0; i <= e; i++)
            if (len[i] == l) R->pool[*pool + k++] = (uint8_t)i;
        code <<= 1;
    }
    if (used == 1)
        for (i = 0; i <= e; i++)
            if (len[i]) R->pool[*pool] = (uint8_t)i;
    if (used > 1 && code != 1u << (MAXL + 1)) return -1;  // not a complete code
    *pool += used;
    return 0;
}

// Bins: values below 8 are their own symbol, then 2 per octave
static unsigned unbin(unsigned s) {
    if (s < 8) return s;
    unsigned n = 3 + ((s - 8) >> 1);
    return ((1u << n) | ((s - 8) & 1) << (n - 1)) + bits(n - 1);
}

// --- Prediction ---

typedef uint32_t __attribute__((may_alias)) word_t;

static void copy(uint8_t* d, const uint8_t* s, int n) {  // d and s aligned alike
    while (n && ((uintptr_t)d & 3)) {
        *d++ = *s++;
        n--;
    }
    for (; n >= 4; n -= 4, d += 4, s += 4) *(word_t*)d = *(const word_t*)s;
    while (n-- > 0) *d++ = *s++;
}

static int clampq(int v, int m) { return v < 0 ? 0 : v > m ? m : v; }

// The spatial modes S, G, A, B: they read this frame's decoded neighbour
static inline unsigned spred(unsigned mode, int p, int m) {
    const uint8_t *cur = s_cur, *prev = s_prev;
    unsigned g = la04_nb[p];
    int n = g & 64 ? 1 : (int)(g & 63);
    if (mode >= M_A && p >= NG + NE + 21) n = 21;  // the same LED on the previous arm
    if (mode == M_S || mode == M_A) return n ? cur[p - n] : 0;
    return n ? (unsigned)clampq(prev[p] + cur[p - n] - prev[p - n], m) : prev[p];
}

// The scan order of the grid under a shift mode: first the positions that
// come into the picture (no source in the previous frame), then the others.
// Built when the shift mode changes.
static const uint8_t* shift_order(unsigned mode) {
    if (s_ordm != mode) {
        unsigned k = 0;
        for (unsigned pass = 0; pass < 2; pass++)
            for (unsigned p = 0; p < NG; p++) {
                unsigned g = la04_nb[p];
                unsigned has = mode == M_L ? g & 128 : mode == M_R ? g & 64 : mode == M_U ? la04_dn[p] : g & 63;
                if (!has == !pass) R->ord[k++] = (uint8_t)p;
            }
        s_ordm = (uint8_t)mode;
    }
    return R->ord;
}

// The transition table's key. Kind 0: (prev >> ts) << tk | prev2 >> ts.
// Kind 1 (q = 8): prev >> 4 << 4 | the bin of prev - prev2 (0; 1..7 for |d|
// in [1], [2, 3], [4, 7] .. [64, 255]; + 7 if negative).
static const uint8_t dbin8[8] = {0, 1, 2, 2, 3, 3, 3, 3};
static const uint8_t dbin_rep[9] = {0, 1, 2, 5, 11, 23, 47, 100, 0};

static inline unsigned tt_key(unsigned a, unsigned b) {
    if (!s_tkind) return (a >> s_ts) << s_tk | b >> s_ts;
    int d = (int)a - (int)b;
    unsigned x = (unsigned)(d < 0 ? -d : d);
    unsigned k = x < 8 ? dbin8[x] : x < 16 ? 4 : x < 32 ? 5 : x < 64 ? 6 : 7;
    return (a >> 4) << 4 | (d < 0 ? k + 7 : k);
}

// One segment (si: 0 the grid, 1 the outline, 2 RGB) of this frame,
// positions start..end-1, under mode md (and outline rotation rot)
static void segment(unsigned si, unsigned md, int rot, int start, int end) {
    unsigned mode = md & 15;
    uint8_t* cur = s_cur;
    const uint8_t *prev = s_prev, *prev2 = s_prev2;
    const uint8_t* ord = 0;
    const int m = s_m;
    int p, n;
    // The temporal modes: first the prediction of the whole segment
    switch (mode) {
        case M_Z:
        case M_T:
            copy(cur + start, prev + start, end - start);
            if (mode == M_Z) return;
            break;
        case M_P: copy(cur + start, prev2 + start, end - start); break;
        case M_X:
            for (p = start; p < end; p++) cur[p] = (uint8_t)clampq(prev[p] + R->tt[tt_key(prev[p], prev2[p])], m);
            break;
        case M_V:
            for (p = start; p < end; p++) cur[p] = (uint8_t)clampq(2 * prev[p] - prev2[p], m);
            break;
        case M_L:
            for (p = start; p < end; p++) cur[p] = la04_nb[p] & 128 ? prev[p + 1] : 0;
            break;
        case M_R:
            for (p = start; p < end; p++) cur[p] = la04_nb[p] & 64 ? prev[p - 1] : 0;
            break;
        case M_U:
            for (p = start; p < end; p++) {
                n = la04_dn[p];
                cur[p] = n ? prev[p + n] : 0;
            }
            break;
        case M_D:
            for (p = start; p < end; p++) {
                n = la04_nb[p] & 63;
                cur[p] = n ? prev[p - n] : 0;
            }
            break;
        case M_ROT:
            for (p = start, n = -rot; p < end; p++, n++) {
                if (n < 0)
                    n += NE;
                else if (n >= NE)
                    n -= NE;
                cur[p] = prev[NG + n];
            }
            break;
    }
    if (mode >= M_L && mode <= M_D) ord = shift_order(mode);
    const int spatial = mode >= M_S && mode <= M_B;
    const uint8_t* mp = R->map + (si * NFAM + (md >> 4)) * NCTX;
    const uint8_t *bp = s_p, *be = s_end;
    uint32_t bb = s_bb;
    int bn = s_bn, i = 0, len = end - start;
    const unsigned nres = s_nres, eob = s_eob, act = s_act;
    unsigned ctx = 3;
    while (i < len) {
        p = ord ? ord[i] : start + i;
        unsigned c = ctx, u = la04_nb[p] & 63, sym;
        if (act) {
            int d = prev[p] - prev2[p], e = u ? cur[p - u] - prev[p - u] : 0;
            unsigned a = (unsigned)((d < 0 ? -d : d) + (e < 0 ? -e : e));
            c += (a > s_thr[0]) * 4 + (a > s_thr[1]) * 4 + (a > s_thr[2]) * 4;
        } else {
            if (prev[p] != prev2[p]) c += 4;
            if (u && cur[p - u] != prev[p - u]) c += 8;
        }
        const htab_t* t = &R->tab[mp[c]];
        while (bn <= 24) {
            bb |= (uint32_t)(bp < be ? *bp : 0) << (24 - bn);
            bp++;
            bn += 8;
        }
        {
            const hlen_t* e = &t->L[t->minl];
            uint32_t v = bb >> 16;
            while (v > e->lim) e++;
            unsigned l = (unsigned)(e - t->L);
            bb <<= l;
            bn -= (int)l;
            sym = R->pool[e->base + (v >> (16 - l))];
        }
        // At least 13 bits are left in bb: enough for the extra bits (<= 6)
        unsigned z = sym;
        if (z >= nres) z -= nres;
        if (z >= 8 && sym < eob) {
            unsigned k = 2 + ((z - 8) >> 1);  // extra bits
            z = ((2u | ((z - 8) & 1)) << k) + (bb >> (32 - k));
            bb <<= k;
            bn -= (int)k;
        }
        if (sym < nres) {  // a residual
            z++;
            int r = z & 1 ? -(int)((z + 1) >> 1) : (int)(z >> 1);
            cur[p] = (uint8_t)(((spatial ? (int)spred(mode, p, m) : cur[p]) + r) & m);
            ctx = z <= 2 ? 1 : 2;
            i++;
        } else {  // a run of positions that are their prediction
            int run = sym >= eob ? len - i : (int)z + 1;
            if (run > len - i) run = len - i;
            if (!spatial)
                i += run;
            else
                for (; run; run--, i++) cur[start + i] = (uint8_t)spred(mode, start + i, m);
            ctx = 0;
        }
    }
    s_p = bp;
    s_bb = bb;
    s_bn = bn;
}

bool LA04_Open(const uint8_t* data, uint32_t length) {
    s_nf = 0;
    if (length < 8 || memcmp(data, "LA04", 4)) return false;
    s_d = data;
    s_end = data + length;
    uint16_t nf = (uint16_t)(data[4] | data[5] << 8);
    s_q = data[7];
    if (!nf || !data[6] || (s_q != 1 && s_q != 2 && s_q != 4 && s_q != 8)) return false;
    s_m = (uint8_t)((1u << s_q) - 1);
    for (unsigned i = 0; i < 16; i++) s_deq[i] = (uint8_t)((i * 255u + s_m / 2u) / s_m);
    // Residual bins for the values 0..2^q-2, run bins for 0..NG-2, the end of
    // the segment
    s_nres = s_q == 1 ? 1 : s_q == 2 ? 3 : s_q == 4 ? 10 : 18;
    s_eob = s_nres + 18;
    seek(64);
    unsigned nt = read_gamma() - 1, pool = 0, i;
    if (nt > TMAX) return false;
    s_nt = (uint8_t)nt;
    s_tup = tell();
    for (i = 0; i < nt; i++) bits(12);
    if (read_table(&R->tab[NTAB], nt + 1 + HOLD_SYMS, &pool)) return false;
    unsigned holds = 0;
    for (i = 0; i < pool; i++) holds |= R->pool[i] > nt;
    if (holds && read_table(&R->tab[NTAB + 1], nt + 1, &pool)) return false;
    unsigned fmask = bits(3 * NFAM), ntab;
    // The transition table, per key (tt_key): runs of default entries
    // (gamma(length + 1)), each followed by one other entry: '0' delta 0, or
    // '1' + a signed delta in lb bits
    if (bits(1)) {
        unsigned tk = s_q < 4 ? s_q : 4, lb = s_q < 8 ? s_q + 1u : 8u, n = 1u << 2 * tk, h, run;
        s_ts = (uint8_t)(s_q - tk);
        s_tk = (uint8_t)tk;
        s_tkind = (uint8_t)(s_q == 8 ? bits(1) : 0);
        h = s_ts ? 1u << (s_ts - 1) : 0;
        for (i = 0; i < n; i++) {
            for (run = read_gamma() - 1; run && i < n; run--, i++) {  // the default: V at the key's centre
                unsigned lo = i & ((1u << tk) - 1);
                int A = (int)(((i >> tk) << s_ts) + h), B;
                if (s_tkind)
                    B = A - (lo > 7 ? -(int)dbin_rep[lo - 7] : (int)dbin_rep[lo]);
                else
                    B = (int)((lo << s_ts) + h);
                R->tt[i] = (int8_t)(clampq(clampq(2 * A - B, s_m) - A + 128, 255) - 128);
            }
            if (run) return false;
            if (i >= n) break;
            int v = 0;
            if (bits(1)) {
                v = (int)bits(lb);
                if (v >> (lb - 1)) v -= 1 << lb;
            }
            R->tt[i] = (int8_t)v;
        }
    }
    s_act = (uint8_t)bits(1);
    if (s_act)
        for (i = 0; i < 3; i++) s_thr[i] = (uint16_t)bits(s_q + 1u);
    ntab = read_gamma() - 1;
    if (ntab > NTAB) return false;
    for (i = 0; i < ntab; i++)
        if (read_table(&R->tab[i], (unsigned)s_eob + 1, &pool)) return false;
    s_tbits = (uint8_t)nbits_for(ntab);
    // The map: per used family, per context c = base + 4 T + 8 U: '0' the
    // first candidate, '10' the second, '11' + bits the table. The
    // candidates (distinct, in this order): the entries of c - 8 and c - 4,
    // the previous coded entry.
    unsigned pm = 0;
    memset(R->map, 0, sizeof(R->map));
    if (ntab > 1)
        for (unsigned sf = 0; sf < 3 * NFAM; sf++) {
            if (!(fmask >> sf & 1)) continue;
            uint8_t* mp = R->map + sf * NCTX;
            if (bits(1)) {  // all the previous entry
                memset(mp, (int)pm, NCTX);
                continue;
            }
            for (unsigned c = 0; c < NCTX; c++) {
                int list[3] = {c >= 8 ? mp[c - 8] : -1, c >= 4 ? mp[c - 4] : -1, (int)pm};
                unsigned cand[2], nc = 0, v;
                for (unsigned k = 0; k < 3; k++)
                    if (list[k] >= 0 && nc < 2 && (!nc || cand[0] != (unsigned)list[k])) cand[nc++] = (unsigned)list[k];
                if (!bits(1))
                    v = cand[0];
                else if (!bits(1))
                    v = nc > 1 ? cand[1] : ntab;
                else
                    v = bits(s_tbits);
                if (v >= ntab) return false;
                mp[c] = (uint8_t)(pm = v);
            }
        }
    s_start = tell();
    s_ordm = 0;
    s_fi = 0;
    s_cur = R->buf[0];
    s_prev = R->buf[1];
    s_prev2 = R->buf[2];
    s_nf = nf;
    return true;
}

uint16_t LA04_Frames(void) { return s_nf; }
uint8_t LA04_Fps(void) { return s_nf ? s_d[6] : 1; }

static unsigned tuple_bits(uint32_t bit, unsigned n) {
    const uint8_t* p = s_d + (bit >> 3);
    uint32_t w = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2];
    return (w >> (24 - (bit & 7) - n)) & ((1u << n) - 1);
}

void LA04_Next(uint8_t* out) {
    if (!s_nf) return;
    uint8_t* t = s_prev2;
    s_prev2 = s_prev;
    s_prev = s_cur;
    s_cur = t;
    if (s_fi == 0) {  // frame 0: the frames before it are black
        seek(s_start);
        for (int i = 0; i < N / 4; i++) ((word_t*)s_prev)[i] = ((word_t*)s_prev2)[i] = 0;
        s_fctx = 0;
        s_hold = 0;
    }
    if (!s_hold) {
        unsigned s = hdec(&R->tab[NTAB + s_fctx]);
        if (s > s_nt) {  // the previous frame, held for this many frames
            s_hold = (uint16_t)(unbin(s - s_nt - 1) + 1);
            s_fctx = 1;
        } else {  // a tuple: the modes of the grid, the outline and RGB
            unsigned tup;
            if (s == s_nt)
                tup = bits(12);
            else
                tup = tuple_bits(s_tup + 12 * s, 12);
            s_fctx = 0;
            unsigned e = (tup >> 4) & 15;
            segment(0, grid_modes[tup >> 8], 0, 0, NG);
            segment(1, edge_modes[e], edge_rot[e], NG, NG + NE);
            segment(2, rgb_modes[tup & 15], 0, NG + NE, N);
        }
    }
    if (s_hold) {
        copy(s_cur, s_prev, N);
        s_hold--;
    }
    if (++s_fi == s_nf) s_fi = 0;
    if (s_q == 8)
        for (int p = 0; p < N; p++) out[layout_order[p]] = s_cur[p];
    else
        for (int p = 0; p < N; p++) out[layout_order[p]] = s_deq[s_cur[p]];
}
