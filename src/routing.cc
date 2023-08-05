#include "routing.h"
#include "logger.h"
#include "device.h"
#include "pnx_utils.h"

#include <arpa/inet.h>
#include <algorithm>


struct RoutingEntry {
    in_addr dest;
    in_addr mask;
    in_addr next_hop;
    int device_id;
protected:
    RoutingEntry() {};
public:
    RoutingEntry(const in_addr dest, const in_addr mask, const in_addr next_hop, int device_id) {
        this->dest = dest;
        this->mask = mask;
        this->next_hop = next_hop;
        this->device_id = device_id;
    }

    bool operator<(const RoutingEntry &rhs) const {
        // bigger mask means more specific.
        return mask.s_addr < rhs.mask.s_addr;
    }
};

struct DirectRoutingEntry : public RoutingEntry {
private:
    in_addr next_hop;
public:
    DirectRoutingEntry(const in_addr dest, const in_addr mask, int device_id) {
        this->dest = dest;
        this->mask = mask;
        this->device_id = device_id;

        this->next_hop.s_addr = 0; // this field should not be used.
    }
};

#include <vector>
std::vector<std::shared_ptr<RoutingEntry>> routing_table_;

static void route_table_init() {
    // initialize from OS routing table.

    // get the OS routing table through popen
    FILE *fp = popen("ip route", "r");
    if (fp == NULL) {
        logError("cannot get routing table from ip route");
        exit(-1);
    }

    // read the routing table line by line
    // the format of each line is like:
    // default via 192.168.65.5 dev eth0 
    // 10.100.1.0/24 via 10.100.4.2 dev veth0-3 
    // 10.100.2.0/24 via 10.100.4.2 dev veth0-3 
    // 10.100.3.0/24 via 10.100.4.2 dev veth0-3 
    // 10.100.4.0/24 dev veth0-3 scope link 

    char buf[1024];
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        if (strstr(buf, "scope link") != NULL) {
            // it is a scope link.
            // parse the ip address and device name.
            char ip_str[1024];
            char device_name[1024];
            int subnet_len;
            sscanf(buf, "%s dev %s scope link", ip_str, device_name);

            // parse the ip address.
            // ip_str looks like 10.100.4.0/24
            // get ip and subnet mask.
            in_addr ip;
            sscanf(ip_str, "%s/%d", ip_str, &subnet_len);
            inet_aton(ip_str, &ip);

            // add the routing entry to the routing table.
            int id = find_device(device_name);
            if (id == -1) {
                logWarning("found a scope link routing entry but no device %s added", device_name);
            } else 
                routing_table_.push_back(std::make_shared<DirectRoutingEntry>(
                    ip, subnet_len_to_mask(subnet_len), id));
        } else {
            // it is not a scope link.
            // parse the ip address, mask, next hop and device name.
            char ip_str[1024];
            char next_hop_str[1024];
            char device_name[1024];
            sscanf(buf, "%s via %s dev %s", ip_str, next_hop_str, device_name);

            // parse the ip address, mask and next hop.
            in_addr ip;
            int subnet_len;
            in_addr next_hop;

            // check if it is a default routing entry.
            if (strcmp(ip_str, "default") == 0) {
                // it is a default routing entry.
                // set the ip_str to "0.0.0.0/0"
                strcpy(ip_str, "0.0.0.0/0");
                subnet_len = 0;
            } else {
                // it is not a default routing entry.
                // parse the ip address and mask.
                sscanf(ip_str, "%s/%d", ip_str, subnet_len);
            }

            inet_aton(ip_str, &ip);
            inet_aton(next_hop_str, &next_hop);

            // add the routing entry to the routing table.
            int id = find_device(device_name);
            if (id == -1) {
                logWarning("found a normal routing entry but no device %s added", device_name);
            } else 
                routing_table_.push_back(std::make_shared<RoutingEntry>(ip, 
                    subnet_len_to_mask(subnet_len), next_hop, id));
        }
    }

    std::sort(routing_table_.begin(), routing_table_.end());
}

std::pair<int, in_addr> get_next_hop(const in_addr dest) {
    static bool initialized = false;
    if (!initialized) {
        route_table_init();
        initialized = true;
    }

    // find the routing entry with the longest prefix match.
    // i.e. from the tail of the routing table.

    for (auto it = routing_table_.rbegin(); it != routing_table_.rend(); it++) {
        auto entry = *it;

        // check if it is a direct routing entry.
        // use dynamic cast.
        auto direct_entry = std::dynamic_pointer_cast<DirectRoutingEntry>(entry);
        if (direct_entry != nullptr) {
            // it is a direct routing entry.
            // check if the subnet number matches.
            if (subnet_match(dest, direct_entry->dest, direct_entry->mask)) {
                return { direct_entry->device_id, dest };
            }
        } else {
            // it is a normal routing entry.
            // check if the subnet number matches.
            if (subnet_match(dest, entry->dest, entry->mask)) {
                return { entry->device_id, entry->next_hop };
            }
        }
    }
    return { -1, {0} };
}