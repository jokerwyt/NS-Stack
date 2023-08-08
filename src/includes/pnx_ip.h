#pragma once

/**
* @file ip.h
* @brief Library supporting sending/receiving IP packets encapsulated
* in an Ethernet II frame. 
*/
#include <netinet/ip.h>
#include <functional>
#include <atomic>


/**
* @brief Send an IP packet to specified host.
* @param src Source IP address.
* @param dest Destination IP address.
* @param proto Value of ‘protocol‘ field in IP header.
* @param buf pointer to IP payload
* @param len Length of IP payload
* @return 0 on success, -1 on error.
*/
int sendIPPacket(const struct in_addr src, const struct in_addr dest,
     int proto, const void *buf, int len);


int ip_packet_handler(const void* buf, int len);

// return 0 for success.
typedef int (*ip_packet_callback)(const void* buf, int len);

int set_ip_packet_callback(ip_packet_callback cb);