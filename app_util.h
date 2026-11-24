// Small helpers for apps that the firmware keeps, so every app need not
// carry its own copy (hh_api_t, HH_API_VERSION 4). No hardware here: the
// emulator and the app tests (tools/app_test/mock_hh.h) use this file too.
#pragma once
#include <stdint.h>

uint8_t AppUtil_Sin8(uint8_t angle);
uint32_t AppUtil_Hsv(uint8_t h, uint8_t s, uint8_t v);
uint32_t AppUtil_Isqrt(uint32_t n);
uint8_t AppUtil_Atan8(int x, int y);
// n in decimal, with a '-' if negative, into text (12 bytes: "-2147483648");
// returns text
char* AppUtil_Decimal(int32_t n, char text[12]);
