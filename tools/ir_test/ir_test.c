// ir_codec.c on the PC: frames built by IrFrame_*, sent through a model
// of the receiver (marks stretched, spaces shortened, jitter, the edge
// timer's whole microseconds), noise between them, decoded by IrRx_*.
//
//   cc -I. tools/ir_test/ir_test.c ir_codec.c -o ir_test && ./ir_test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ir_codec.h"

static int checks, failures;
#define CHECK(cond, ...)                      \
    do {                                      \
        checks++;                             \
        if (!(cond)) {                        \
            failures++;                       \
            printf("FAIL  " __VA_ARGS__);     \
            printf("  (line %d)\n", __LINE__); \
        }                                     \
    } while (0)

static unsigned s_seed = 1;
static unsigned rnd(unsigned n) {
    s_seed = s_seed * 1103515245u + 12345u;
    return (s_seed >> 8) % n;
}

typedef struct {
    int nec, repeats, packets;
    uint32_t nec_data;
    uint8_t packet[IR_FRAME_BYTES];
} result_t;

static void feed(ir_rx_t* rx, result_t* r, int mark, uint32_t us) {
    switch (IrRx_Interval(rx, mark, us)) {
        case IR_RX_NEC:
            r->nec++;
            r->nec_data = rx->bytes[0] | rx->bytes[1] << 8 | rx->bytes[2] << 16 | (uint32_t)rx->bytes[3] << 24;
            break;
        case IR_RX_NEC_REPEAT: r->repeats++; break;
        case IR_RX_PACKET:
            r->packets++;
            memcpy(r->packet, rx->bytes, sizeof r->packet);
            break;
        default: break;
    }
}

// Sends f to rx as the receiver module would put it out: each mark longer
// by `stretch` us, each space shorter by as much, plus or minus `jitter`.
// flip: flip this segment's length to the other bit value (a bit error).
static void send(ir_rx_t* rx, result_t* r, const ir_frame_t* f, int stretch, int jitter, int flip) {
    uint16_t n = IrFrame_Segments(f);
    for (uint16_t i = 0; i < n; i++) {
        int us = IrFrame_SegmentUs(f, i);
        if (i == flip) us = us == IR_ONE_SPACE ? IR_ZERO_SPACE : IR_ONE_SPACE;
        int mark = !(i & 1);
        us += mark ? stretch : -stretch;
        if (jitter) us += (int)rnd(2 * jitter + 1) - jitter;
        feed(rx, r, mark, (uint32_t)us);
    }
    // the gap after the frame: a long space (the edge timer stops at 0x7FFF)
    feed(rx, r, 0, 0x7FFF);
}

static void noise(ir_rx_t* rx, result_t* r) {
    for (int i = 0, k = rnd(6); i < k; i++) {
        feed(rx, r, 1, 50 + rnd(3000));
        feed(rx, r, 0, 50 + rnd(6000));
    }
}

int main(void) {
    static const int stretch[] = {0, 60, 100, 150}, jitter[] = {0, 40, 90};
    ir_rx_t rx;
    ir_frame_t f;

    // Packets of every length, every timing, with and without noise before
    for (int len = 0; len <= IR_PACKET_DATA_MAX; len++) {
        for (unsigned s = 0; s < sizeof stretch / sizeof *stretch; s++)
            for (unsigned j = 0; j < sizeof jitter / sizeof *jitter; j++)
                for (int with_noise = 0; with_noise < 2; with_noise++) {
                    uint8_t data[IR_PACKET_DATA_MAX], type = (uint8_t)rnd(256);
                    for (int i = 0; i < len; i++) data[i] = (uint8_t)rnd(256);
                    CHECK(IrFrame_Packet(&f, type, data, (uint8_t)len), "packet of %d bytes is built", len);
                    result_t r = {0};
                    IrRx_Reset(&rx);
                    if (with_noise) noise(&rx, &r);
                    send(&rx, &r, &f, stretch[s], jitter[j], -1);
                    CHECK(r.packets == 1 && r.nec == 0, "%d-byte packet, stretch %d, jitter %d, noise %d: %d packets",
                          len, stretch[s], jitter[j], with_noise, r.packets);
                    CHECK(r.packet[0] == type && r.packet[1] == len && !memcmp(&r.packet[2], data, (size_t)len),
                          "%d-byte packet: the bytes", len);
                }
    }
    CHECK(!IrFrame_Packet(&f, 'X', (uint8_t[IR_PACKET_DATA_MAX + 1]){0}, IR_PACKET_DATA_MAX + 1),
          "a packet over %d bytes is refused", IR_PACKET_DATA_MAX);

    // The length on air: 6.75 ms + 8 * (n + 3) bits
    IrFrame_Packet(&f, 'H', (const uint8_t*)"0123456789", 10);
    unsigned total = 0;
    for (uint16_t i = 0; i < IrFrame_Segments(&f); i++) total += IrFrame_SegmentUs(&f, i);
    CHECK(total > 150000 && total < 280000, "a 10-byte packet takes %u us", total);

    // A bit error: the CRC catches it, every bit position
    IrFrame_Packet(&f, 'H', (const uint8_t*)"HACKER HOTEL", 12);
    int caught = 0;
    for (int bit = 0; bit < f.bits; bit++) {
        result_t r = {0};
        IrRx_Reset(&rx);
        send(&rx, &r, &f, 80, 30, 3 + 2 * bit);  // segment 3 + 2k: bit k's space
        caught += r.packets == 0;
    }
    // a flipped length bit makes the receiver wait for more or fewer bytes:
    // it then misses the frame, which is fine too
    CHECK(caught == f.bits, "a flipped bit is never taken (%d of %d caught)", caught, f.bits);

    // A cut-off packet, then a good one: the good one comes through
    {
        result_t r = {0};
        IrRx_Reset(&rx);
        IrFrame_Packet(&f, 'R', (const uint8_t*)"ALICE", 5);
        uint16_t n = IrFrame_Segments(&f);
        for (uint16_t i = 0; i < n / 2; i++) feed(&rx, &r, !(i & 1), IrFrame_SegmentUs(&f, i));
        feed(&rx, &r, 0, 0x7FFF);
        send(&rx, &r, &f, 100, 50, -1);
        CHECK(r.packets == 1, "after a cut-off packet, the next one decodes (%d)", r.packets);
    }

    // NEC frames and repeat codes still decode, also right after a packet
    for (unsigned s = 0; s < sizeof stretch / sizeof *stretch; s++)
        for (unsigned j = 0; j < sizeof jitter / sizeof *jitter; j++) {
            uint32_t data = 0x00FF00FFu ^ (rnd(256) * 0x01010101u & 0x00FF00FFu);  // address, ~address, command, ~command
            result_t r = {0};
            IrRx_Reset(&rx);
            IrFrame_Packet(&f, 'F', (const uint8_t*)"\x01\x02\x03", 3);
            send(&rx, &r, &f, stretch[s], jitter[j], -1);
            IrFrame_Nec(&f, data);
            send(&rx, &r, &f, stretch[s], jitter[j], -1);
            IrFrame_NecRepeat(&f);
            send(&rx, &r, &f, stretch[s], jitter[j], -1);
            send(&rx, &r, &f, stretch[s], jitter[j], -1);
            CHECK(r.packets == 1 && r.nec == 1 && r.repeats == 2 && r.nec_data == data,
                  "stretch %d, jitter %d: packet %d, NEC %d (%08x), repeats %d", stretch[s], jitter[j], r.packets, r.nec,
                  r.nec_data, r.repeats);
        }

    // Leaders are told apart: a NEC leader mark never starts a packet (the
    // split is 6.75 ms), and after a short mark the space decides
    {
        result_t r = {0};
        IrRx_Reset(&rx);
        IrFrame_Nec(&f, 0x12ED34CBu);
        f.leader_mark = 6000;  // a short leader mark with a 4.5 ms space: Samsung32, never a packet
        send(&rx, &r, &f, 0, 0, -1);
        CHECK(r.nec == 1 && r.packets == 0, "a 6 ms leader mark and a 4.5 ms space: a Samsung32 frame, not a packet");
        IrFrame_Packet(&f, 'H', (const uint8_t*)"X", 1);
        f.leader_mark = 7000;  // a long packet leader: taken as NEC (infrared.c checks ~command)
        send(&rx, &r, &f, 0, 0, -1);
        CHECK(r.packets == 0, "a 7 ms leader never gives a packet");
    }

    // Samsung32 (a Samsung TV remote): a 4.5 ms + 4.5 ms leader, then NEC
    // bits; a packet's leader space (2.25 ms) keeps packets apart from it
    for (int s = 0; s < 4; s++)
        for (int j = 0; j < 3; j++) {
            result_t r = {0};
            IrRx_Reset(&rx);
            uint32_t data = 0xF8070707u;  // TV, Vol+
            IrFrame_Nec(&f, data);
            f.leader_mark = IR_SAMSUNG_LEADER_MARK;
            f.leader_space = IR_SAMSUNG_LEADER_SPACE;
            send(&rx, &r, &f, stretch[s], jitter[j], -1);
            IrFrame_Packet(&f, 'H', (const uint8_t*)"HI", 2);
            send(&rx, &r, &f, stretch[s], jitter[j], -1);
            CHECK(r.nec == 1 && r.nec_data == data && r.packets == 1 && r.packet[0] == 'H',
                  "stretch %d, jitter %d: Samsung32 %d (%08x), then a packet %d", stretch[s], jitter[j], r.nec,
                  r.nec_data, r.packets);
        }
    {
        result_t r = {0};
        IrRx_Reset(&rx);
        IrFrame_Packet(&f, 'H', (const uint8_t*)"X", 1);
        f.leader_space = IR_SAMSUNG_LEADER_SPACE;  // a packet with a Samsung leader: 32 bits of it as NEC
        send(&rx, &r, &f, 0, 0, -1);
        CHECK(r.packets == 0, "a 4.5 ms leader space never gives a packet");
    }

    printf("%s  ir_codec: %d of %d checks\n", failures ? "FAIL" : "ok  ", checks - failures, checks);
    return failures != 0;
}
