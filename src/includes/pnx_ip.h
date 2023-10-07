#pragma once

/**
* @file ip.h
* @brief Library supporting sending/receiving IP packets encapsulated
* in an Ethernet II frame. 
*/
#include <netinet/ip.h>
#include <functional>
#include <atomic>
#include <memory>


/**
* @brief Send an IP packet to specified host.
* @param src Source IP address.
* @param dest Destination IP address.
* @param proto Value of ‘protocol‘ field in IP header.
* @param buf pointer to IP payload
* @param len Length of IP payload
* @return 0 on success, -1 on error.
*/
int ip_send_packet(const struct in_addr src, const struct in_addr dest,
     int proto, std::shared_ptr<char[]> buf, int len);


int ip_packet_handler(const void* buf, int len);

// return 0 for success.
// buf points to the beginning of the IP packet (include IP header).
typedef int (*ip_packet_callback)(const void* buf, int len);

int set_ip_packet_callback(ip_packet_callback cb);