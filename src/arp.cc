#include "arp.h"

#include <map>
#include <mutex>
#include <cstring>
#include <condition_variable>
#include "packetio.h"
#include "device.h"
#include "logger.h"
#include "pnx_utils.h"

struct InAddrHash {
    std::size_t operator()(const in_addr& addr) const {
        return std::hash<uint32_t>()(addr.s_addr);
    }
};

static std::mutex mtx_; // a big lock for the whole ARP module
static std::unordered_map<in_addr, ether_addr, InAddrHash> cache_;

// A cv will be constructed when an ARP request is sent.
// It's shared by every thread that wants to query the same 
// IP when waiting for response.
// The cv will notify all waiting threads when the ARP reply is received.
// And it will be extracted from the map. After every shared_ptr to 
// it is destructed, the cv will be destructed and freed.
static std::unordered_map<in_addr, std::shared_ptr<std::condition_variable>, 
    InAddrHash> outstanding_requests_;

static const int kARPTimeout = 50; // milliseconds

int ARPQuery(int dev_id, const in_addr target_ip, ether_addr* target_mac) {
    std::unique_lock<std::mutex> lock(mtx_);

    if (cache_.find(target_ip) != cache_.end()) {
        *target_mac = cache_[target_ip];
        return 0;
    }

    if (outstanding_requests_.find(target_ip) != outstanding_requests_.end()) {
        auto cv = outstanding_requests_[target_ip];
        bool timeout = cv->wait_for(lock, std::chrono::milliseconds(kARPTimeout),
            [&]() { return cache_.count(target_ip); });
        
        if (timeout) {
            logError("ARP request timeout. dev_id=%d, for ip %s", dev_id, inet_ntoa_safe(target_ip));
            return -1;
        }
        
        *target_mac = cache_[target_ip];
        return 0;
    }

    // send ARP request
    // construct it first
    static char frame[ETHER_MAX_LEN];
    struct ether_arp *arp = (struct ether_arp*)frame;
    struct arphdr *arp_header = &arp->ea_hdr;

    // fill in the arp header
    arp_header->ar_hrd = htons(ARPHRD_ETHER);
    arp_header->ar_pro = htons(ETHERTYPE_IP);
    arp_header->ar_hln = ETH_ALEN;
    arp_header->ar_pln = sizeof(in_addr);
    arp_header->ar_op = htons(ARPOP_REQUEST);

    // fill in the arp payload
    memcpy(arp->arp_sha, dev_mac(dev_id)->ether_addr_octet, ETH_ALEN);
    memcpy(arp->arp_spa, &dev_ip(dev_id)->s_addr, sizeof(in_addr));
    memset(arp->arp_tha, 0, ETH_ALEN);
    memcpy(arp->arp_tpa, &target_ip, sizeof(in_addr));

    int result = send_frame(frame, sizeof(struct ether_arp), ETHERTYPE_ARP, &kBroadcast, dev_id);
    if (result != 0) {
        logError("fail to send ARP request. dev_id=%d", dev_id);
        return result;
    }

    auto cv = std::make_shared<std::condition_variable>();
    outstanding_requests_[target_ip] = cv;

    // give away the lock and wait the cv for kARPTimeout milliseconds
    bool timeout = cv->wait_for(lock, std::chrono::milliseconds(kARPTimeout), [&]() {
        return cache_.count(target_ip);
    });

    if (timeout) {
        logError("ARP request timeout. dev_id=%d, for ip %s", dev_id, inet_ntoa_safe(target_ip));
        return -1;
    }

    *target_mac = cache_[target_ip];
    return 0;
}

int ARPHandler(int dev_id, const char* ether_frame) {
    std::unique_lock<std::mutex> guard(mtx_);

    const struct ether_arp *arp = (struct ether_arp*)(ether_frame + ETH_HLEN);
    if (arp->ea_hdr.ar_op == htons(ARPOP_REPLY)) {

        in_addr target_ip;
        ether_addr target_mac;

        memcpy(&target_ip, arp->arp_spa, sizeof(in_addr));
        memcpy(&target_mac, arp->arp_sha, ETH_ALEN);
        cache_.emplace(target_ip, target_mac);

        auto cv = outstanding_requests_.extract(target_ip).mapped();
        cv->notify_all();
        return 0;
        
    } else if (arp->ea_hdr.ar_op == htons(ARPOP_REQUEST)) {
        
        // reply back the ARP request
        // construct it
        static char eth_payload[ETHER_MAX_LEN];
        struct ether_arp *arp_reply = (struct ether_arp*)eth_payload;
        struct arphdr *arp_header = &arp_reply->ea_hdr;

        // fill in the arp header
        arp_header->ar_hrd = htons(ARPHRD_ETHER);
        arp_header->ar_pro = htons(ETHERTYPE_IP);
        arp_header->ar_hln = ETH_ALEN;
        arp_header->ar_pln = sizeof(in_addr);
        arp_header->ar_op = htons(ARPOP_REPLY);

        // fill in the arp body
        memcpy(arp_reply->arp_sha, dev_mac(dev_id)->ether_addr_octet, ETH_ALEN);
        memcpy(arp_reply->arp_spa, &dev_ip(dev_id)->s_addr, sizeof(in_addr));
        memcpy(arp_reply->arp_tha, arp->arp_sha, ETH_ALEN);
        memcpy(arp_reply->arp_tpa, arp->arp_spa, sizeof(in_addr));

        int result = send_frame(eth_payload, sizeof(struct ether_arp), 
            ETHERTYPE_ARP, (ether_addr*)arp->arp_sha, dev_id);
        
        if (result != 0) {
            logWarning("fail to send ARP reply. dev_id=%d", dev_id);
            return result;
        }
        return 0;

    } else {
        logWarning("recv unknown ARP packet. op=%d", arp->ea_hdr.ar_op);
        return -1;
    }
}




bool operator==(const in_addr& lhs, const in_addr& rhs) {
    return lhs.s_addr == rhs.s_addr;
}