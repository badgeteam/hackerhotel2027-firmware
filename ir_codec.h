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
//   8.4 ms + 4.2 ms a shot (JVC timing, 526 us marks, 526 / 1574 us
//                   spaces): 16 bits, then silence. The Fri3d Camp 2022
//                   Time Blaster's and the Hacker Hotel 2025 banana's.
//                   The receiver takes it for a NEC leader; 16 bits and
//                   then nothing make it a shot. See IrShot_Decode().
//   4.5 ms + 2.25 ms a packet: type, length n (0 .. IR_PACKET_DATA_MAX),
//                   n data bytes, CRC-8 (polynomial 0x07) over the bytes
//                   before it. A packet is badge to badge (name swap,
//                   games); its leader keeps it apart from remote
//                   controls' frames.
//
// A packet with n data bytes takes 6.75 ms + (n + 3) * 8 bits of 1.1 or
// 2.2 ms: 0.2 s for a 10-letter name, 0.65 s at most.
//
// A raw frame (IrFrame_Raw) is any list of marks and spaces, on a carrier
// of its own: for other remote controls (Sony, RC5, ...). The receiver
// only knows the frames above.
#include <stdbool.h>
#include <stdint.h>

#define IR_CARRIER_HZ 38000  // every frame but a raw one
#define IR_RAW_CARRIER_MIN 20000
#define IR_RAW_CARRIER_MAX 60000  // Ir_CarrierPeriods() stays within 32 bits

#define IR_NEC_LEADER_MARK 9000
#define IR_NEC_LEADER_SPACE 4500
#define IR_NEC_REPEAT_SPACE 2250
#define IR_SAMSUNG_LEADER_MARK 4500
#define IR_SAMSUNG_LEADER_SPACE 4500
#define IR_PACKET_LEADER_MARK 4500
#define IR_PACKET_LEADER_SPACE 2250
#define IR_SHOT_LEADER_MARK 8400  // JVC timing: the banana takes 7 to 9 ms
#define IR_SHOT_LEADER_SPACE 4200
#define IR_SHOT_BIT_MARK 526
#define IR_SHOT_ZERO_SPACE 526
#define IR_SHOT_ONE_SPACE 1574
#define IR_BIT_MARK 562
#define IR_ZERO_SPACE 562
#define IR_ONE_SPACE 1687

#define IR_PACKET_DATA_MAX 32
#define IR_FRAME_BYTES (IR_PACKET_DATA_MAX + 3)  // type, length, data, CRC

// A frame to send: its leader and its bits, or a raw frame's marks and spaces
typedef struct {
    uint16_t leader_mark, leader_space;  // us
    uint16_t bits;                       // 0 (a repeat code) .. 8 * IR_FRAME_BYTES
    uint8_t jvc;                         // the bits in JVC timing (a shot), not NEC
    uint8_t bytes[IR_FRAME_BYTES];
    uint16_t carrier_hz;
    const uint16_t* raw;  // a raw frame: its marks and spaces in us, in the caller's array; else NULL
    uint16_t raw_count;
} ir_frame_t;

void IrFrame_Nec(ir_frame_t* f, uint32_t data);  // 32 bits, as received
void IrFrame_NecRepeat(ir_frame_t* f);
void IrFrame_Shot(ir_frame_t* f, uint16_t bits);  // 16 bits, CRC included (IrShot_Encode)
// false if length > IR_PACKET_DATA_MAX
bool IrFrame_Packet(ir_frame_t* f, uint8_t type, const uint8_t* data, uint8_t length);
// count marks and spaces, starting with a mark, each us[i] us long; the
// frame keeps the pointer, so us must stay as it is until it has gone out.
// false if count is 0 or the carrier is outside IR_RAW_CARRIER_MIN .. MAX.
bool IrFrame_Raw(ir_frame_t* f, const uint16_t* us, uint16_t count, uint32_t carrier_hz);

// The frame as alternating marks and spaces, starting with a mark:
// IrFrame_Segments() of them, segment i lasting IrFrame_SegmentUs(f, i)
uint16_t IrFrame_Segments(const ir_frame_t* f);
uint16_t IrFrame_SegmentUs(const ir_frame_t* f, uint16_t i);
// us as whole periods of a carrier of hz, rounded, at least 1
uint16_t Ir_CarrierPeriods(uint32_t us, uint32_t hz);

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
    IR_RX_SHOT,        // 16 bits after a NEC-like leader, then silence: bytes[0..1] (IrShot_Decode)
} ir_rx_event_t;

// After this long without an edge, a frame that stopped after 16 bits is a
// shot (longer than any bit's space, shorter than the gap between frames)
#define IR_SHOT_SILENCE_US 4000

void IrRx_Reset(ir_rx_t* rx);
// mark: the interval that just ended was a mark (carrier on), else a space
ir_rx_event_t IrRx_Interval(ir_rx_t* rx, bool mark, uint32_t us);
// No edge for us microseconds (the line is idle): ends a shot, which has
// no edge after its stop mark until the next frame
ir_rx_event_t IrRx_Silence(ir_rx_t* rx, uint32_t us);

// A shot's 16 bits (Fri3d Camp 2022 Time Blaster, Hacker Hotel 2025
// banana), LSB first: team (3 bits), trigger (1), command (4), parameter
// (4), CRC (4). Commands: 1 shoot, 2 heal (parameter: the channel), 8 set
// team, 10 one byte of text (team << 5 | trigger << 4 | parameter).
typedef struct {
    uint8_t team, trigger, command, parameter;
} ir_shot_t;

// The CRC of a shot's low 12 bits, as the Time Blaster's documentation gives it
uint8_t IrShot_Crc(uint16_t bits);
// false if the CRC is wrong
bool IrShot_Decode(uint16_t bits, ir_shot_t* shot);
uint16_t IrShot_Encode(const ir_shot_t* shot);  // with its CRC
