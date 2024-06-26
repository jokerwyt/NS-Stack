#include "pnx_ip.h"

#include "routing.h"
#include "arp.h"
#include "logger.h"
#include "pnx_utils.h"
#include "packetio.h"
#include "device.h"
#include "pnx_tcp.h"
#include "ringbuffer.h"
#include "gracefully_shutdown.h"

#include <arpa/inet.h>
#include <thread>
#include <mutex>
#include <atomic>


static std::atomic<ip_packet_callback> ip_callback{nullptr};

int set_ip_packet_callback(ip_packet_callback cb) {
    ip_callback.store(cb);
    return 0;
}


// calc the checksum of the ip header without modifying the ip header.
static uint16_t calc_iphd_checksum(struct iphdr *ip_header) {
    // calc the checksum of the ip header using the RFC specified algorithm.
    // https://tools.ietf.org/html/rfc1071

    // first set the checksum field to 0
    auto old_check = ip_header->check;

    ip_header->check = 0;

    /* Compute Internet Checksum for "count" bytes
    *         beginning at location "addr".
    */
    uint32_t sum = 0;

    int count = ip_header->ihl * 4;
    uint16_t * addr = (uint16_t *) ip_header;
    while( count > 1 )  {
        /*  This is the inner loop */
        sum += * (uint16_t*) addr++;
        count -= 2;
    }

    /*  Add left-over byte, if any */
    if ( count > 0 )
        sum += * (unsigned char *) addr;

    /*  Fold 32-bit sum to 16 bits */
    while (sum>>16)
        sum = (sum & 0xffff) + (sum >> 16);

    ip_header->check = old_check;
    return ~(uint16_t)sum;
}

static bool verify_iphd_checksum(struct iphdr *ip_header) {
    return ip_header->check == calc_iphd_checksum(ip_header);
}

static int _ip_send_packet(const in_addr src, const in_addr dest, int proto,
                 std::shared_ptr<char[]> buf, int len) {


    // steps.
    // 1. query routing table, get the target ip.
    // 2. ARP query, get the target mac.
    // 3. send the packet.

    auto routing = get_next_hop(dest);
    int dev_id = routing.first;
    in_addr next_hop_ip = routing.second;
    if (dev_id == -1) {
        logWarning("cannot find next hop for %s", inet_ntoa_safe(dest).get());
        return -1;
    }

    // ARP query
    struct ether_addr dest_mac;
    if (ARPQuery(dev_id, next_hop_ip, &dest_mac) != 0) {
        logWarning("cannot find ARP entry for %s", inet_ntoa_safe(next_hop_ip).get());
        return -1;
    }

    // construct the ethernet payload. i.e. the IP packet.
    char packet[ETHER_MAX_LEN];

    if ((size_t)len > ETHER_MAX_LEN - sizeof(struct iphdr)) {
        logError("IP Packet too large, fragmentation not supported yet.");
        return -1;
    }
    
    struct iphdr *ip_header = (struct iphdr*)packet;
    ip_header->ihl = 5;
    ip_header->version = 4;
    ip_header->tos = 0;
    ip_header->tot_len = htons(sizeof(struct iphdr) + len);
    ip_header->id = 0;
    ip_header->frag_off = 0;
    ip_header->ttl = 64;
    ip_header->protocol = proto;
    // ip_header->check = 0;
    ip_header->saddr = src.s_addr;
    ip_header->daddr = dest.s_addr;

    // calc the checksum of the ip header using the RFC specified algorithm.
    // https://tools.ietf.org/html/rfc1071

    ip_header->check = calc_iphd_checksum(ip_header);

    memcpy(packet + sizeof(struct iphdr), buf.get(), len);

    // send the packet
    return send_frame(packet, ntohs(ip_header->tot_len), ETHERTYPE_IP, &dest_mac, dev_id);
}

static BlockingRingBuffer<std::tuple<in_addr, in_addr, int, std::shared_ptr<char[]>, int>, 100> ip_sending_buffer;
int ip_send_packet(const in_addr src, const in_addr dest, int proto,
                 std::shared_ptr<char[]> buf, int len) {
    // all sending task is forward to a new thread to prevent ARP deadlock.

    static std::mutex mutex;
    static bool init_done = false;
    std::lock_guard<std::mutex> lock(mutex);
    
    if (init_done == false) {
        init_done = true;
        static std::atomic<bool> stop{false};

        std::thread t = std::thread([]() {
            while (stop.load() == false) {
                std::optional<std::tuple<in_addr, in_addr, int, std::shared_ptr<char[]>, int>> task;
                task = ip_sending_buffer.pop();
                if (!task.has_value()) {
                    continue;
                }
                auto result = _ip_send_packet(std::get<0>(task.value()), 
                    std::get<1>(task.value()), std::get<2>(task.value()), 
                    std::get<3>(task.value()), std::get<4>(task.value()));
                
                if (result != 0) {
                    logWarning("fail to send IP packet");
                }
            }
        });
        t.detach();

        add_exit_clean_up([&]() {
            stop.store(true);
        }, EXIT_CLEAN_UP_PRIORITY_IP_SENDING);
    }

    while (ip_sending_buffer.push(std::make_tuple(src, dest, proto, buf, len)) != true);
    return 0;
}


int ip_packet_handler(const void *buf, int len) {
    // steps. 
    // 1. do tons of sanity check.
    // 2. check if routing needed.
    // 3. check if the packet is for me. 
    // (i.e. the destination is one of my devices' ip addresses)
    // 4. otherwise, drop it.
    
    struct iphdr *ip_header = (struct iphdr*)buf;

    {   // tons of sanity check.
        // verify the checksum
        if (!verify_iphd_checksum(ip_header)) {
            logWarning("IP header checksum error");
            return -1;
        }

        // verify the version
        if (ip_header->version != 4) {
            logWarning("IP version error");
            return -1;
        }

        // verify the header length
        if (ip_header->ihl < 5) {
            logWarning("IP header length error");
            return -1;
        }

        // verify the TTL
        if (ip_header->ttl == 0) {
            logWarning("IP packet dropped due to TTL=0.");
            return -1;
        }

        // verify the protocol
        // if (ip_header->protocol != IPPROTO_ICMP && 
        //     ip_header->protocol != IPPROTO_TCP && 
        //     ip_header->protocol != IPPROTO_UDP) {
        //     logWarning("IP protocol error");
        //     return -1;
        // }
    }

    // check if routing needed.
    // if the destination is one of my devices' ip addresses, 
    // get_next_hop will give me the corresponding device. 
    // Then we can know whether routing is needed by checking the ip address of the device.
    auto rt = get_next_hop(in_addr{ip_header->daddr});
    auto dev_id = rt.first;
    auto next_hop_ip = rt.second;

    if (dev_id == -1) {
        logWarning("IP forward: cannot find next hop for %s", inet_ntoa_safe(in_addr{ip_header->daddr}).get());
        return -1;
    }

    // check if the packet is for me.
    if (dev_ip(dev_id)->s_addr == ip_header->daddr) {
        // the packet is for me.
        // pass it to the upper layer.
        // for now we just log it.

        logInfo("a IP packet for me: %s. src=%s", 
            inet_ntoa_safe(in_addr{ip_header->daddr}).get(), 
            inet_ntoa_safe(in_addr{ip_header->saddr}).get());
        

        // pass it to the upper layer.
        tcp_segment_handler((char*)buf + ip_header->ihl * 4, len - ip_header->ihl * 4, 
            in_addr{ip_header->saddr}, in_addr{ip_header->daddr});
        
        // if the callback is set, call it.
        if (ip_callback.load() != nullptr) {
            return ip_callback.load()(buf, len);
        }

        return 0;
    }

    // otherwise, forward it.
    // decrement the TTL
    ip_header->ttl--;
    if (ip_header->ttl == 0) {
        logWarning("IP packet dropped due to TTL=0.");
        return -1;
    }

    // recalc the checksum
    ip_header->check = calc_iphd_checksum(ip_header);


    // get next hop mac address
    struct ether_addr dest_mac;
    if (ARPQuery(dev_id, next_hop_ip, &dest_mac) != 0) {
        logWarning("cannot find ARP entry for %s", inet_ntoa_safe(next_hop_ip).get());
        return -1;
    }

    // forward the packet
    logTrace("forwarding IP packet to %s", inet_ntoa_safe(next_hop_ip).get());
    int result = send_frame(buf, len, ETHERTYPE_IP, &dest_mac, dev_id);

    if (result != 0) {
        logWarning("fail to forward IP packet");
        return -1;
    }
    
    return 0;
}