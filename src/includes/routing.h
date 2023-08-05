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


