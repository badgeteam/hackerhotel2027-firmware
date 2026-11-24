#pragma once
// Swapping names with another badge over infrared (issue #8).
//
// SAY HI in the menu sends HELLO with the owner's name (persist.h). A
// badge that gets it answers with REPLY and its own name, and both show
// "HI <name>". HELLO goes out again every 1.5 s, 4 times at most, until a
// REPLY comes; then the badge shows "NO REPLY". Outside apps a badge
// always answers a HELLO, whatever it shows. While an app runs, the app
// gets the packets (hhapp.h).
//
// The packets (infrared.h): type 'H' (HELLO) or 'R' (REPLY), and the
// name as data, 0 to 32 bytes, no NUL. An empty name shows as "HI!".
#include <stdbool.h>
#include <stdint.h>

#define GREET_HELLO 'H'
#define GREET_REPLY 'R'

// Starts sending HELLO
void Greet_SayHi(void);

// Ends what shows (a button, the menu)
void Greet_Cancel(void);

// Call every main loop pass outside apps and the menu: takes the packets
// that came in, answers a HELLO, and sends. Returns true while the
// greeting has the LEDs; then call Greet_Draw() instead of drawing what
// ran before.
bool Greet_Tick(uint32_t elapsed_ms);
void Greet_Draw(void);
