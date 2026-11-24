#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "ir_codec.h"  // IR_PACKET_DATA_MAX

// Infrared, sent with the IR LED (D315, cathode on PB21) and received with
// the 38kHz IR receiver module (S1, output on PB19): NEC frames, as remote
// controls send, and packets, from badge to badge (ir_codec.h has both
// formats).
// Samsung32 frames (Samsung TV remotes) arrive as 16-bit extended NEC,
// address 0x0707 for a Samsung TV. A held Samsung button repeats the whole
// frame, so it never sets `repeat`.
//
// Uses TIM2 (38kHz carrier, remapped to PB21) and EXTI line 19.

typedef struct {
    uint16_t address;  // 8-bit for standard NEC, 16-bit for extended NEC
    uint8_t command;
    bool repeat;       // NEC repeat code (button held), address/command copied from the last frame
} infrared_nec_message_t;

void infrared_init(void);

// Stops IR, parks the LED pin (off, driven) and disables the receiver
// interrupt. Returns whether IR was initialized, so callers can restore it.
bool infrared_deinit(void);

// Start sending a frame in the background; returns false while a previous
// transmission is still in progress. A frame takes ~68ms; per NEC, leave at
// least 108ms between the start of consecutive frames/repeats.
// Addresses above 0xFF are sent as extended (16-bit) NEC, others as standard
// NEC (address followed by its inverse).
bool infrared_send_nec(uint16_t address, uint8_t command);
bool infrared_send_nec_raw(uint32_t data);  // 32 bits, sent LSB first
bool infrared_send_nec_repeat(void);
bool infrared_tx_busy(void);

// Fetch the next received message; returns false when there is none.
bool infrared_receive(infrared_nec_message_t* message);

// Packets: a type, then up to IR_PACKET_DATA_MAX (32) data bytes, with a
// CRC on air. The receiver keeps up to 3 that came in with a good CRC;
// more are dropped until they are taken.
typedef struct {
    uint8_t type;
    uint8_t length;
    uint8_t data[IR_PACKET_DATA_MAX];
} infrared_packet_t;

// Start sending a packet in the background: 0.65 s at most (6.75 ms + 1.1
// or 2.2 ms per bit, 8 * (length + 3) bits). False while something else
// goes out, or if length > IR_PACKET_DATA_MAX.
bool infrared_send_packet(uint8_t type, const uint8_t* data, uint8_t length);
bool infrared_receive_packet(infrared_packet_t* packet);

// A shot from a Fri3d Camp 2022 Time Blaster or a Hacker Hotel 2025 banana
// (ir_codec.h), with a good CRC; false when none came in. Up to 7 are kept.
bool infrared_receive_shot(ir_shot_t* shot);
// Send a shot's 16 bits (IrShot_Encode: CRC included) in JVC timing; false
// while something still goes out. A banana takes it only in its mode 0.
bool infrared_send_shot(uint16_t bits);

// The marks and spaces that came in since the last call, oldest first: bit
// 15 set for a mark, the rest its length in us (32767: that or longer).
// Up to 127 are kept, our own transmission too (the receiver sees the
// badge's own LED), so it doubles as a self-test. For looking at other
// devices' frames (tools/badge.py irlog).
unsigned infrared_raw_read(uint16_t* out, unsigned max);
