#pragma once
// The infrared frames on air, without the hardware (infrared.c sends and
// receives them; tools/ir_test tests this file on the PC).
//
// All frames use NEC pulse-distance coding on a 38 kHz carrier: a leader
// (a mark, then a space), then each bit as a 562 us mark and a 562 us (0)
// or 1687 us (1) space, least significant bit of each byte first, then a
// 562 us stop mark. A mark is carrier on.
//
//   leader          what follows
//   9 ms + 4.5 ms   a NEC frame: 32 bits (address, ~address or a 16-bit
//                   address, command, ~command)
//   9 ms + 2.25 ms  a NEC repeat code: no bits
//   4.5 ms + 4.5 ms a Samsung32 frame (Samsung TV remotes): 32 bits as
//                   NEC, the address byte twice (0x07 0x07 for a TV)
//   4.5 ms + 2.25 ms a packet: type, length n (0 .. IR_PACKET_DATA_MAX),
//                   n data bytes, CRC-8 (polynomial 0x07) over the bytes
//                   before it. A packet is badge to badge (name swap,
//                   games); its leader keeps it apart from remote
//                   controls' frames.
//
// A packet with n data bytes takes 6.75 ms + (n + 3) * 8 bits of 1.1 or
// 2.2 ms: 0.2 s for a 10-letter name, 0.65 s at most.
#include <stdbool.h>
#include <stdint.h>

#define IR_NEC_LEADER_MARK 9000
#define IR_NEC_LEADER_SPACE 4500
#define IR_NEC_REPEAT_SPACE 2250
#define IR_SAMSUNG_LEADER_MARK 4500
#define IR_SAMSUNG_LEADER_SPACE 4500
#define IR_PACKET_LEADER_MARK 4500
#define IR_PACKET_LEADER_SPACE 2250
#define IR_BIT_MARK 562
#define IR_ZERO_SPACE 562
#define IR_ONE_SPACE 1687

#define IR_PACKET_DATA_MAX 32
#define IR_FRAME_BYTES (IR_PACKET_DATA_MAX + 3)  // type, length, data, CRC

// A frame to send: its leader and its bits
typedef struct {
    uint16_t leader_mark, leader_space;  // us
    uint16_t bits;                       // 0 (a repeat code) .. 8 * IR_FRAME_BYTES
    uint8_t bytes[IR_FRAME_BYTES];
} ir_frame_t;

void IrFrame_Nec(ir_frame_t* f, uint32_t data);  // 32 bits, as received
void IrFrame_NecRepeat(ir_frame_t* f);
// false if length > IR_PACKET_DATA_MAX
bool IrFrame_Packet(ir_frame_t* f, uint8_t type, const uint8_t* data, uint8_t length);

// The frame as alternating marks and spaces, starting with a mark:
// IrFrame_Segments() of them, segment i lasting IrFrame_SegmentUs(f, i)
uint16_t IrFrame_Segments(const ir_frame_t* f);
uint16_t IrFrame_SegmentUs(const ir_frame_t* f, uint16_t i);

uint8_t Ir_Crc8(const uint8_t* p, unsigned n);

// The receiver: fed each mark or space as it ends, with its length
typedef struct {
    uint8_t state;
    uint8_t packet;  // receiving a packet, not a NEC frame (after a short leader mark: a packet or Samsung32)
    uint16_t bits;   // received so far
    uint16_t want;   // bits in the whole frame, once known
    uint8_t bytes[IR_FRAME_BYTES];
} ir_rx_t;

typedef enum {
    IR_RX_NOTHING = 0,
    IR_RX_NEC,         // a NEC or Samsung32 frame: its 32 bits are in bytes[0..3]
    IR_RX_NEC_REPEAT,  // a NEC repeat code
    IR_RX_PACKET,      // a packet with a good CRC: type bytes[0], length bytes[1], data from bytes[2]
} ir_rx_event_t;

void IrRx_Reset(ir_rx_t* rx);
// mark: the interval that just ended was a mark (carrier on), else a space
ir_rx_event_t IrRx_Interval(ir_rx_t* rx, bool mark, uint32_t us);
