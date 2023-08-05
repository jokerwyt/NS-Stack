#include "pnx_utils.h"

#include <assert.h>
#include <time.h>
#include <stdio.h>

long long get_time_ns() {
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) != -1);
    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

long long get_time_us() {
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) != -1);
    return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

char* mac_to_str(const unsigned char* mac, char buf[6]) {
    sprintf(buf, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", 
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

unsigned char* str_to_mac(const char str[6], unsigned char* mac) {
    sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", 
        &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    return mac;
}

in_addr subnet_len_to_mask(int subnet_len) {
    in_addr mask;
    mask.s_addr = 0;
    for (int i = 0; i < subnet_len; i++) {
        mask.s_addr |= 1 << (31 - i);
    }
    return mask;
}

bool subnet_match(const in_addr ip1, const in_addr ip2, const in_addr mask) {
    return (ip1.s_addr & mask.s_addr) == (ip2.s_addr & mask.s_addr);
}
