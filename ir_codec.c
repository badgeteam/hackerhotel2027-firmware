#include "ir_codec.h"
#include <string.h>

// ---- Frames to send ----

// A frame of a leader and bits, on the 38 kHz carrier
static void frame(ir_frame_t* f, uint16_t leader_mark, uint16_t leader_space, uint16_t bits, uint8_t jvc) {
    f->leader_mark = leader_mark;
    f->leader_space = leader_space;
    f->bits = bits;
    f->jvc = jvc;
    f->carrier_hz = IR_CARRIER_HZ;
    f->raw = NULL;
}

void IrFrame_Nec(ir_frame_t* f, uint32_t data) {
    frame(f, IR_NEC_LEADER_MARK, IR_NEC_LEADER_SPACE, 32, 0);
    for (int i = 0; i < 4; i++) f->bytes[i] = (uint8_t)(data >> (8 * i));
}

void IrFrame_Shot(ir_frame_t* f, uint16_t bits) {
    frame(f, IR_SHOT_LEADER_MARK, IR_SHOT_LEADER_SPACE, 16, 1);
    f->bytes[0] = (uint8_t)bits;
    f->bytes[1] = (uint8_t)(bits >> 8);
}

void IrFrame_NecRepeat(ir_frame_t* f) { frame(f, IR_NEC_LEADER_MARK, IR_NEC_REPEAT_SPACE, 0, 0); }

uint8_t Ir_Crc8(const uint8_t* p, unsigned n) {
    uint8_t c = 0;
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; i++) c = (uint8_t)(c & 0x80 ? (c << 1) ^ 0x07 : c << 1);
    }
    return c;
}

bool IrFrame_Packet(ir_frame_t* f, uint8_t type, const uint8_t* data, uint8_t length) {
    if (length > IR_PACKET_DATA_MAX) return false;
    frame(f, IR_PACKET_LEADER_MARK, IR_PACKET_LEADER_SPACE, (uint16_t)(8 * (length + 3)), 0);
    f->bytes[0] = type;
    f->bytes[1] = length;
    if (length) memcpy(&f->bytes[2], data, length);
    f->bytes[2 + length] = Ir_Crc8(f->bytes, 2u + length);
    return true;
}

bool IrFrame_Raw(ir_frame_t* f, const uint16_t* us, uint16_t count, uint32_t carrier_hz) {
    if (!count || carrier_hz < IR_RAW_CARRIER_MIN || carrier_hz > IR_RAW_CARRIER_MAX) return false;
    f->carrier_hz = (uint16_t)carrier_hz;
    f->raw = us;
    f->raw_count = count;
    return true;
}

uint16_t IrFrame_Segments(const ir_frame_t* f) { return f->raw ? f->raw_count : (uint16_t)(2 + 2 * f->bits + 1); }

uint16_t IrFrame_SegmentUs(const ir_frame_t* f, uint16_t i) {
    if (f->raw) return f->raw[i];
    if (i == 0) return f->leader_mark;
    if (i == 1) return f->leader_space;
    i -= 2;
    if (!(i & 1)) return f->jvc ? IR_SHOT_BIT_MARK : IR_BIT_MARK;  // a bit's mark, or the stop mark
    unsigned bit = i / 2;
    bool one = (f->bytes[bit / 8] >> (bit % 8)) & 1;
    if (f->jvc) return one ? IR_SHOT_ONE_SPACE : IR_SHOT_ZERO_SPACE;
    return one ? IR_ONE_SPACE : IR_ZERO_SPACE;
}

uint16_t Ir_CarrierPeriods(uint32_t us, uint32_t hz) {
    uint32_t n = (us * hz + 500000) / 1000000;
    return n ? (uint16_t)n : 1;
}

// ---- Receiving ----

enum {
    RX_IDLE = 0,      // waiting for a leader mark
    RX_LEADER_SPACE,  // after a leader mark
    RX_BIT_MARK,      // expecting a bit's mark
    RX_BIT_SPACE,     // expecting a bit's space: 562 us (0) or 1687 us (1)
    RX_REPEAT_MARK,   // expecting the stop mark of a repeat code
    RX_STOP_MARK,     // the frame is in; its stop mark ends it
};

// Receivers stretch marks and shorten spaces, and the badge's clock is an
// RC oscillator: allow 40% either way. A 4.5 ms space after a 9 ms mark is
// tried before 2.25 ms (their ranges touch).
static bool near(uint32_t us, uint32_t nominal) { return us > nominal * 6 / 10 && us < nominal * 14 / 10; }

// The two leader marks split halfway, at 6.75 ms, so their ranges do not
// overlap; after the short one (a packet's or Samsung32's), so do the two
// spaces, at 3.375 ms
#define LEADER_SPLIT ((IR_NEC_LEADER_MARK + IR_PACKET_LEADER_MARK) / 2)
#define SHORT_SPACE_SPLIT ((IR_SAMSUNG_LEADER_SPACE + IR_PACKET_LEADER_SPACE) / 2)

// Bits get wider windows, as the previous firmware had: a receiver may add
// 150 us to a mark and take as much off a space, plus jitter. A space
// longer than halfway between 562 and 1687 us is a 1.
static bool bit_mark(uint32_t us) { return us > 250 && us < 1000; }
static bool bit_space(uint32_t us) { return us > 250 && us < 2400; }

void IrRx_Reset(ir_rx_t* rx) { rx->state = RX_IDLE; }

ir_rx_event_t IrRx_Interval(ir_rx_t* rx, bool mark, uint32_t us) {
    switch (rx->state) {
        case RX_IDLE:
            if (mark && us >= LEADER_SPLIT && near(us, IR_NEC_LEADER_MARK)) {
                rx->packet = 0;
                rx->state = RX_LEADER_SPACE;
            } else if (mark && us < LEADER_SPLIT && near(us, IR_PACKET_LEADER_MARK)) {
                rx->packet = 1;
                rx->state = RX_LEADER_SPACE;
            }
            return IR_RX_NOTHING;

        case RX_LEADER_SPACE:
            if (mark) break;
            if (rx->packet && us >= SHORT_SPACE_SPLIT) {
                if (!near(us, IR_SAMSUNG_LEADER_SPACE)) break;
                rx->packet = 0;  // Samsung32: 32 bits, as NEC
                rx->bits = 0;
                rx->want = 32;
                rx->state = RX_BIT_MARK;
                return IR_RX_NOTHING;
            }
            if (near(us, rx->packet ? IR_PACKET_LEADER_SPACE : IR_NEC_LEADER_SPACE)) {
                rx->bits = 0;
                rx->want = rx->packet ? 16 : 32;  // a packet: until its length byte is in
                rx->state = RX_BIT_MARK;
                return IR_RX_NOTHING;
            }
            if (!rx->packet && near(us, IR_NEC_REPEAT_SPACE)) {
                rx->state = RX_REPEAT_MARK;
                return IR_RX_NOTHING;
            }
            break;

        case RX_BIT_MARK:
            if (!mark || !bit_mark(us)) break;
            rx->state = RX_BIT_SPACE;
            return IR_RX_NOTHING;

        case RX_BIT_SPACE: {
            if (!mark && rx->bits == 16 && us >= IR_SHOT_SILENCE_US) return IrRx_Silence(rx, us);
            if (mark || !bit_space(us)) break;
            unsigned bit = rx->bits++;
            if (bit % 8 == 0) rx->bytes[bit / 8] = 0;
            if (us > (IR_ZERO_SPACE + IR_ONE_SPACE) / 2) rx->bytes[bit / 8] |= (uint8_t)(1u << (bit % 8));
            if (rx->packet && rx->bits == 16) {
                if (rx->bytes[1] > IR_PACKET_DATA_MAX) break;
                rx->want = (uint16_t)(8 * (rx->bytes[1] + 3));
            }
            if (rx->bits < rx->want) {
                rx->state = RX_BIT_MARK;
                return IR_RX_NOTHING;
            }
            rx->state = RX_STOP_MARK;
            if (!rx->packet) return IR_RX_NEC;
            unsigned n = rx->bytes[1] + 2u;
            return Ir_Crc8(rx->bytes, n) == rx->bytes[n] ? IR_RX_PACKET : IR_RX_NOTHING;
        }

        case RX_REPEAT_MARK:
            rx->state = RX_IDLE;
            return mark && bit_mark(us) ? IR_RX_NEC_REPEAT : IR_RX_NOTHING;

        case RX_STOP_MARK:
            rx->state = RX_IDLE;
            if (mark && bit_mark(us)) return IR_RX_NOTHING;
            break;
    }
    // Out of step: this interval may be the start of a new leader
    rx->state = RX_IDLE;
    return mark ? IrRx_Interval(rx, mark, us) : IR_RX_NOTHING;
}

ir_rx_event_t IrRx_Silence(ir_rx_t* rx, uint32_t us) {
    // after the 16th bit's space came its stop mark (or the 17th bit's
    // mark), and since then nothing
    if (rx->state != RX_BIT_SPACE || rx->packet || rx->bits != 16 || us < IR_SHOT_SILENCE_US) return IR_RX_NOTHING;
    rx->state = RX_IDLE;
    return IR_RX_SHOT;
}

// ---- Shots ----

uint8_t IrShot_Crc(uint16_t bits) {
    unsigned d[12];
    for (int i = 0; i < 12; i++) d[i] = bits >> i & 1;
    unsigned c0 = d[11] ^ d[10] ^ d[9] ^ d[8] ^ d[6] ^ d[4] ^ d[3] ^ d[0];
    unsigned c1 = d[8] ^ d[7] ^ d[6] ^ d[5] ^ d[3] ^ d[1] ^ d[0] ^ 1;
    unsigned c2 = d[9] ^ d[8] ^ d[7] ^ d[6] ^ d[4] ^ d[2] ^ d[1] ^ 1;
    unsigned c3 = d[10] ^ d[9] ^ d[8] ^ d[7] ^ d[5] ^ d[3] ^ d[2];
    return (uint8_t)(c0 | c1 << 1 | c2 << 2 | c3 << 3);
}

bool IrShot_Decode(uint16_t bits, ir_shot_t* shot) {
    if (IrShot_Crc(bits) != bits >> 12) return false;
    shot->team = bits & 7;
    shot->trigger = bits >> 3 & 1;
    shot->command = bits >> 4 & 15;
    shot->parameter = bits >> 8 & 15;
    return true;
}

uint16_t IrShot_Encode(const ir_shot_t* shot) {
    uint16_t bits = (uint16_t)((shot->team & 7) | (shot->trigger & 1) << 3 | (shot->command & 15) << 4 | (shot->parameter & 15) << 8);
    return (uint16_t)(bits | IrShot_Crc(bits) << 12);
}
