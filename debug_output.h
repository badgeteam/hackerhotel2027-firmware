#pragma once

void debug_output_write(const char* buf, int size);
// The texts one after the other, then a line break (NULL ends the list):
// the firmware's log, without printf's formatting code
void debug_log(const char* first, ...);