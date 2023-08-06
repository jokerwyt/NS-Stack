#include "pnx_ip.h"

#include "routing.h"
#include "arp.h"
#include "logger.h"
#include "pnx_utils.h"
#include "packetio.h"
#include "device.h"

#include <arpa/inet.h>

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

int sendIPPacket(const in_addr src, const in_addr dest, int proto,
                 const void* buf, int len) {
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

    memcpy(packet + sizeof(struct iphdr), buf, len);

    // send the packet
    return send_frame(packet, ntohs(ip_header->tot_len), ETHERTYPE_IP, &dest_mac, dev_id);
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

        // verify the total length
        if (ntohs(ip_header->tot_len) != len) {
            logWarning("IP total length error");
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

        return 0;
    }

    // otherwise, forward it.
    // decrement the TTL
    ip_header->ttl--;
    if (ip_header->ttl == 0) {
        logWarning("IP packet dropped due to TTL=0.");
        return -1;
    }

    // direct this packet to the next hop.
    ip_header->daddr = next_hop_ip.s_addr;

    // recalc the checksum
    ip_header->check = calc_iphd_checksum(ip_header);

    // forward the packet
    return send_frame(buf, len, ETHERTYPE_IP, dev_mac(dev_id), dev_id);
}