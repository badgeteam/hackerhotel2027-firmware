#pragma once
#include <stdbool.h>
#include <stdint.h>

// NEC infrared remote protocol, sent with the IR LED (D315, cathode on PB21)
// and received with the 38kHz IR receiver module (S1, output on PB19).
// Received too: Samsung32 (Samsung TV remotes), NEC with a 4.5ms leader
// mark and the address byte sent twice; it arrives as 16-bit extended NEC,
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
