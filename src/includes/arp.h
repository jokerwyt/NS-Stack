#pragma once


#include <netinet/ether.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <memory>
#include <semaphore.h>


// a very simplified implementation of ARP.
// the cache is never expired, for now. 
// If needed, a TTL mechanism can be added.


// query the MAC address.
// return 0 on success, -1 on error.
// the acquired mac addr is placed at target_mac 
// blocking, slow.
int ARPQuery(int dev_id, const struct in_addr target_ip, struct ether_addr *target_mac);

// called by ether frame receiver.
// ether_frame points to the very header of the ethernet frame.
int ARPHandler(int dev_id, const char * ether_frame);
