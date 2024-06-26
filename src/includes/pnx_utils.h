#pragma once

#include <arpa/inet.h>
#include <memory>

// #define PNX_CAST(type, src) (*(type*) &(src))

// get time in nanosecond
long long get_time_ns();

// get time in microsecond
long long get_time_us();

// get MAC str from six-byte array
char* mac_to_str(const unsigned char* mac, char buf[6]);

// get six-byte array from MAC str
unsigned char* str_to_mac(const char str[6], unsigned char* mac);


// return in host byte order
struct in_addr subnet_len_to_mask(int subnet_len);

bool subnet_match(const struct in_addr ip1, const struct in_addr ip2, const struct in_addr mask);


std::unique_ptr<const char[]> inet_ntoa_safe(const struct in_addr in);

// 1 for success.
bool split_ip_subnet(const char * ip_subnet, char *ip, int *subnet_len);


int pnx_init_all_devices();
