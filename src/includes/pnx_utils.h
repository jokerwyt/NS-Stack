#pragma once

#define NO_WARN_CAST(type, dst, src)                                \
    _Pragma("GCC diagnostic push")                                  \
    _Pragma("GCC diagnostic ignored \"-Wpointer-to-int-cast\"")     \
    _Pragma("GCC diagnostic ignored \"-Wint-to-pointer-cast\"")     \
        (dst) = ((type) (src));                                     \
    _Pragma("GCC diagnostic pop")



// get time in nanosecond
long long get_time_ns();


// get time in microsecond
long long get_time_us();

// get MAC str from six-byte array
char* mac_to_str(const unsigned char* mac, char buf[6]);

// get six-byte array from MAC str
unsigned char* str_to_mac(const char str[6], unsigned char* mac);