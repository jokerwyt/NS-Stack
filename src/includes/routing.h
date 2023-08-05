#pragma once

#include <netinet/ether.h>
#include <netinet/ip.h>
#include <memory>


/* 

This file should include a homemade routing table protocol.

But for now we just use the OS-specify routing table.

*/


// return a pair of (device_id, next_hop_ip).
// or (-1, _) if not found.
std::pair<int, in_addr> get_next_hop(const struct in_addr dest);


/**
* @brief Manully add an item to routing table. Useful when talking
with real Linux machines. *
* @param dest The destination IP prefix.
* @param mask The subnet mask of the destination IP prefix.
* @param nextHopMAC MAC address of the next hop.
* @param device Name of device to send packets on.
* @return 0 on success, -1 on error 
*/
int add_routing_entry(const struct in_addr dest, const struct in_addr mask, 
    const struct in_addr next_hop, const char *device);

