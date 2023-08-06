#include "pnx_utils.h"

#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <cstring>

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
        mask.s_addr |= 1 << i;
    }
    return mask;
}

bool subnet_match(const in_addr ip1, const in_addr ip2, const in_addr mask) {
    return (ip1.s_addr & mask.s_addr) == (ip2.s_addr & mask.s_addr);
}

std::unique_ptr<const char[]> inet_ntoa_safe(const in_addr in) {
    char* buf = new char[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &in, buf, INET_ADDRSTRLEN);
    return std::unique_ptr<const char[]>(buf);
}

bool split_ip_subnet(const char* ip_subnet, char* ip, int* subnet_len) {
    // ip_subnet should be like 100.100.100.100/24
    // get the index of /
    int slash_idx = -1;
    for (int i = 0; ip_subnet[i] != '\0'; i++) {
        if (ip_subnet[i] == '/') {
            slash_idx = i;
            break;
        }
    }

    if (slash_idx == -1) {
        // no slash found
        return false;
    }

    // copy the ip part
    strncpy(ip, ip_subnet, slash_idx);

    // copy the subnet_len part
    *subnet_len = atoi(ip_subnet + slash_idx + 1);

    if (*subnet_len < 0 || *subnet_len > 32) {
        // subnet_len is not valid
        return false;
    }
    
    return true;
}
