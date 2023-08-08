#include "routing.h"
#include "logger.h"
#include "device.h"
#include "pnx_utils.h"
#include "rustex.h"
#include "packetio.h"

#include <arpa/inet.h>
#include <algorithm>


struct RoutingEntry : public std::enable_shared_from_this<RoutingEntry> {
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

    virtual ~RoutingEntry() {}

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


// static_routing_table_ comes from add_static_routing_entry.
// including all neighbour subnet routing entries (direct entries).
// dynamic_routing_table_ comes from distance_vec_.  
// direct entries have no priority. 
// we sort them with the normal entries by mask.
// dynamic_routing_table_ does not contain direct entries.

static
std::vector<std::shared_ptr<RoutingEntry>> 
    static_routing_table_, 
    dynamic_routing_table_;


#include <mutex>

static 
std::mutex routing_table_mtx_; // lock of the routing tables


static void route_table_init_from_OS() {
    // initialize from OS routing table.

    std::unique_lock<std::mutex> lock(routing_table_mtx_);

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
            char ipsubnet_str[128];
            char ipstr[128];
            char device_name[128];
            int subnet_len;
            sscanf(buf, "%s dev %s scope link", ipsubnet_str, device_name);

            // parse the ip address.
            // ip_str looks like 10.100.4.0/24
            // get ip and subnet mask.
            in_addr ip;

            // get ipstr and subnet_len
            split_ip_subnet(ipsubnet_str, ipstr, &subnet_len);


            inet_aton(ipstr, &ip);

            // add the routing entry to the routing table.
            int id = find_device(device_name);
            if (id == -1) {
                logWarning("found a scope link routing entry but no device %s added", device_name);
            } else 
                static_routing_table_.push_back(std::make_shared<DirectRoutingEntry>(
                    ip, subnet_len_to_mask(subnet_len), id));
        } else {
            // it is not a scope link.
            // parse the ip address, mask, next hop and device name.
            char ipsubnet_str[128];
            char ipstr[128];
            char next_hop_str[128];
            char device_name[128];
            sscanf(buf, "%s via %s dev %s", ipsubnet_str, next_hop_str, device_name);

            // parse the ip address, mask and next hop.
            in_addr ip;
            int subnet_len;
            in_addr next_hop;

            // check if it is a default routing entry.
            if (strcmp(ipsubnet_str, "default") == 0) {
                // it is a default routing entry.
                // set the ip_str to "0.0.0.0/0"
                strcpy(ipstr, "0.0.0.0");
                subnet_len = 0;
            } else {
                // it is not a default routing entry.
                // parse the ip address and mask.
                split_ip_subnet(ipsubnet_str, ipstr, &subnet_len);
            }

            inet_aton(ipstr, &ip);
            inet_aton(next_hop_str, &next_hop);

            // add the routing entry to the routing table.
            int id = find_device(device_name);
            if (id == -1) {
                logWarning("found a normal routing entry but no device %s added", device_name);
            } else 
                static_routing_table_.push_back(std::make_shared<RoutingEntry>(ip, 
                    subnet_len_to_mask(subnet_len), next_hop, id));
        }
    }

    std::sort(static_routing_table_.begin(), static_routing_table_.end());

    // print the routing table.
    logDebug("routing table:");
    for (auto entry : static_routing_table_) {
        logDebug("dest=%s, mask=%s, next_hop=%s, device=%s, is_direct=%d", 
            inet_ntoa_safe(entry->dest).get(), inet_ntoa_safe(entry->mask).get(), 
            inet_ntoa_safe(entry->next_hop).get(), get_device_name(entry->device_id),
            std::dynamic_pointer_cast<DirectRoutingEntry>(entry) != nullptr);
    }
}

std::pair<int, in_addr> get_next_hop(const in_addr dest) {
    // we dont need to initialize it from OS any more.
    static bool initialized = true; 
    if (!initialized) {
        route_table_init_from_OS();
        initialized = true;
    }

    // find the routing entry with the longest prefix match.
    // i.e. from the tail of the routing table.



    std::unique_lock<std::mutex> lock(routing_table_mtx_);
    // combines two routing tables
    // we suppose the number of entries is small. dont worry about performance.
    std::vector<std::shared_ptr<RoutingEntry>> routing_table;
    routing_table.reserve(static_routing_table_.size() + dynamic_routing_table_.size());
    routing_table.insert(routing_table.end(), static_routing_table_.begin(), static_routing_table_.end());
    routing_table.insert(routing_table.end(), dynamic_routing_table_.begin(), dynamic_routing_table_.end());

    std::sort(routing_table.begin(), routing_table.end());
    lock.unlock();

    for (auto it = routing_table.rbegin(); it != routing_table.rend(); it++) {
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

int add_static_routing_entry(const in_addr dest, const in_addr mask,
                      const struct in_addr next_hop, const char *device, bool direct) {

    // check if the device exists.
    int id = find_device(device);
    if (id == -1) {
        logWarning("cannot add routing entry: device %s not found", device);
        return -1;
    }

    std::unique_lock<std::mutex> lock(routing_table_mtx_);

    // check if the routing entry already exists.
    for (auto entry : static_routing_table_) {
        if (entry->device_id == id && entry->dest.s_addr == dest.s_addr && entry->mask.s_addr == mask.s_addr) {
            logError("conflict routing entry: device %s, dest %s, mask %s", 
                device, inet_ntoa_safe(dest), inet_ntoa_safe(mask));
            return -1;
        }
    }

    // add the routing entry.

    if (!direct)
        static_routing_table_.push_back(std::make_shared<RoutingEntry>(dest, mask, next_hop, id));
    else
        static_routing_table_.push_back(std::make_shared<DirectRoutingEntry>(dest, mask, id));

    std::sort(static_routing_table_.begin(), static_routing_table_.end());
    return 0;
}


// ====== dynamic routing ======

struct DistanceEntry {
    // for routing purpose, we only concern about the subnet number.
    in_addr ip;             
    in_addr mask;

    int hops;

    in_addr updated_from;   // for the routing table update.
    int recv_dev_id;

    DistanceEntry() {}
    DistanceEntry(const in_addr ip, const in_addr mask, int hops) {
        this->ip.s_addr = ip.s_addr & mask.s_addr;
        this->mask = mask;
        this->hops = hops;
    }

    bool operator==(const DistanceEntry &rhs) const {
        return this->subnet().s_addr == rhs.subnet().s_addr 
            && mask.s_addr == rhs.mask.s_addr;
    }


    inline in_addr subnet() const {
        in_addr subnet;
        subnet.s_addr = ip.s_addr & mask.s_addr;
        return subnet;
    }


    static const int kSizeOnwire = sizeof(ip.s_addr) + sizeof(mask.s_addr) + sizeof(hops);

    std::string to_bytes() const {
        std::string bytes;
        // use network order
        uint32_t ip = htonl(this->ip.s_addr);
        uint32_t mask = htonl(this->mask.s_addr);
        uint32_t hops = htonl(this->hops);
        
        bytes.append((char *)&ip, sizeof(ip));
        bytes.append((char *)&mask, sizeof(mask));
        bytes.append((char *)&hops, sizeof(hops));

        return bytes;
    }

    static DistanceEntry from_bytes(const std::string &bytes) {
        DistanceEntry entry;
        // use network order
        uint32_t ip;
        uint32_t mask;
        uint32_t hops;
        memcpy(&ip, bytes.data(), sizeof(ip));
        memcpy(&mask, bytes.data() + sizeof(ip), sizeof(mask));
        memcpy(&hops, bytes.data() + sizeof(ip) + sizeof(mask), sizeof(hops));
        entry.ip.s_addr = ntohl(ip);
        entry.mask.s_addr = ntohl(mask);
        entry.hops = ntohl(hops);

        return entry;
    }
};

struct DEHash {
    size_t operator()(const DistanceEntry &entry) const {
        return entry.ip.s_addr & entry.mask.s_addr;
    }
};

#include <unordered_set>
static rustex::mutex<std::unordered_set<DistanceEntry, DEHash>> distance_vec_{};

static std::string distance_vec_to_bytes() {
    auto dv = distance_vec_.lock();
    std::string bytes;
    // the first 4 bytes is the number of entries.
    int dcnt = get_dev_cnt();
    uint32_t num = htonl((int)(dv->size() + dcnt));
    bytes.append((char *)&num, sizeof(num));

    // then append all entries.
    for (auto entry : *dv) {
        bytes.append(entry.to_bytes());
    }

    // add scope links
    for (int id = 0; id < dcnt; id++) {
        bytes.append(DistanceEntry{*dev_ip(id), *dev_mask(id), 0}.to_bytes());
    }

    return bytes;
}

// return -1 when error, 0 when success without upd, 1 when success with upd.
static int resolve_distance_upd(const int recv_dev_id, const in_addr source, const std::string &bytes) {
    // parse the bytes.
    // the first 4 bytes is the number of entries.
    uint32_t num;
    memcpy(&num, bytes.data(), sizeof(num));
    num = ntohl(num);

    // check the length.
    if (bytes.size() < sizeof(num) + num * DistanceEntry::kSizeOnwire) {
        logError("too short distance upd.");
        return -1;
    }

    auto dv = distance_vec_.lock_mut();

    // get every entry to update the current distance vector.
    int updated = 0;
    for (int i = 0; i < (int)num; i++) {
        DistanceEntry entry = 
            DistanceEntry::from_bytes(
                bytes.substr(sizeof(num) + i * DistanceEntry::kSizeOnwire, 
                DistanceEntry::kSizeOnwire));

        // filter out scope link
        if (get_dev_from_subnet(entry.ip, entry.mask) != -1)
            continue;

        entry.updated_from = source;
        entry.recv_dev_id = recv_dev_id;
        entry.hops++;

        // check if the entry is in the distance vector.
        auto it = dv->find(entry);
        if (it == dv->end()) {
            // it is not in the distance vector.
            // add it.
            dv->insert(entry);
            updated = 1;
        } else {
            // it is in the distance vector.
            // check if the hops is smaller.
            if (it->hops > entry.hops) {
                // the hops is smaller.
                // update it.
                dv->erase(it);
                dv->insert(entry);
                updated = 1;
            } else {
                // the hops is not smaller.
                // ignore it.
                continue;
            }
        }
    }
    return updated;
}

static 
std::vector<std::shared_ptr<RoutingEntry>> generate_dynamic_routing_table_from_dv() {
    std::vector<std::shared_ptr<RoutingEntry>> routing_table;
    auto dv = distance_vec_.lock();

    // print the distance vector
    logDebug("distance vector:");
    for (auto entry : *dv) {
        logDebug("dest=%s, mask=%s, hops=%d, updated_from=%s", 
            inet_ntoa_safe(entry.ip).get(), inet_ntoa_safe(entry.mask).get(), 
            entry.hops, inet_ntoa_safe(entry.updated_from).get());
    }

    // for each entry in the distance vector, generate a routing entry.
    for (auto entry : *dv) {
        routing_table.push_back(std::make_shared<RoutingEntry>(entry.ip, entry.mask, 
            entry.updated_from, get_dev_from_subnet(entry.updated_from, entry.mask)));
    }

    std::sort(routing_table.begin(), routing_table.end());

    // print the routing table.
    logDebug("dynamic routing table:");
    for (auto entry : routing_table) {
        logDebug("dest=%s, mask=%s, next_hop=%s, device=%s", 
            inet_ntoa_safe(entry->dest).get(), inet_ntoa_safe(entry->mask).get(), 
            inet_ntoa_safe(entry->next_hop).get(), get_device_name(entry->device_id));
    }

    return routing_table;
}

int distance_upd_handler(int dev_id, const char *payload, size_t payload_len) {
    struct in_addr from;
    memcpy(&from, payload, sizeof(from));

    // construct a string from the buffer
    std::string bytes(payload + sizeof(from), payload_len - sizeof(from));

    int result = resolve_distance_upd(dev_id, from, bytes);

    if (result < 0) {
        logError("fail to resolve routing update.");
        return -1;
    } else if (result > 0) {
        // update the routing table.
        std::unique_lock<std::mutex> lock(routing_table_mtx_);
        dynamic_routing_table_ = std::move(generate_dynamic_routing_table_from_dv());
        logInfo("routing table updated.");
    }
    return 0;
}


#include <thread>

static const int kRoutingPeriod = 1000; // ms
int fire_distance_upd_daemon() {
    std::thread t = std::thread([]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kRoutingPeriod));
            
            // send distance upd to all neighbors.
            // broadcast to all devices.
            std::string dv_bytes = distance_vec_to_bytes();
            
            int dcnt = get_dev_cnt();
            for (int id = 0; id < dcnt; id++) {
                // send the distance upd.
                
                std::string payload;
                auto ip = dev_ip(id);
                payload.append((char *)&ip->s_addr, sizeof(ip->s_addr));
                payload.append(dv_bytes);

                if (send_frame(payload.data(), payload.size(), kRoutingProtocolCode, &kBroadcast, id) != 0) {
                    logError("fail to send routing upd to device %s", get_device_name(id));
                }
            }
        }
    });
    
    // check sucess.
    if (t.joinable()) {
        t.detach();
        return 0;
    } else {
        logError("fail to fire routing upd timer.");
        return -1;
    }
}
