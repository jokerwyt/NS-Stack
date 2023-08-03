#pragma once

#define PNX_CAST(type, src) (*(type*) &(src))

// get time in nanosecond
long long get_time_ns();

// get time in microsecond
long long get_time_us();

// get MAC str from six-byte array
char* mac_to_str(const unsigned char* mac, char buf[6]);

// get six-byte array from MAC str
unsigned char* str_to_mac(const char str[6], unsigned char* mac);