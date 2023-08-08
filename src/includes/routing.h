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
* @brief Add an item to routing table. 
* @param dest The destination IP prefix.
* @param mask The subnet mask of the destination IP prefix.
* @param nextHopMAC MAC address of the next hop.
* @param device Name of device to send packets on.
* @param direct Whether the destination is a direct neighbour.
* @return 0 on success, -1 on error 
*/
int add_static_routing_entry(const struct in_addr dest, const struct in_addr mask, 
    const struct in_addr next_hop, const char *device, bool direct);




/*
PNX dynamic routing protocol.

* Ethernet based, using a special EtherType.
* broadcast to every neighbour every X seconds.
* every node should have a daemon to handle the broadcast.

frame format:
* <src IP> 4 bytes
* <number of DV entries> 4 bytes
* <entry 1><entry 2><entry 3>...

*/


// return 0 on success, -1 on error
// buf to eth payload
int distance_upd_handler(int dev_id, const char *payload, size_t payload_len);

int fire_distance_upd_daemon();

const uint16_t kRoutingProtocolCode = 0x1234;