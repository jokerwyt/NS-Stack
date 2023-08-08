#include "common.h"
#include "packetio.h"
#include "device.h"
#include "pnx_ip.h"
#include "arp.h"
#include "routing.h"

#include <pcap.h>
#include <atomic>
#include <thread>

static std::atomic<FrameReceiveCallback> recv_callback{nullptr};

int send_frame(const void* buf, int len, int ethtype, const ether_addr* destmac, int id) {
    char frame[ETHER_MAX_LEN];

    struct ether_header *eth_header;
    assert(ETH_HLEN == sizeof(struct ether_header));

    size_t frame_length = ETH_HLEN + len + ETHER_CRC_LEN;
    if (frame_length > ETHER_MAX_LEN) {
        logError("try to send a too large eth frame. frame_len=%u", frame_length);
        return -1;
    }

    if (dev_mac(id) == NULL) {
        logError("no mac address for device %d", id);
        return -1;
    }

    eth_header = (struct ether_header*) frame;

    memcpy(eth_header->ether_shost, dev_mac(id), ETH_ALEN);
    memcpy(eth_header->ether_dhost, destmac->ether_addr_octet, ETH_ALEN);
    eth_header->ether_type = htons(ethtype);

    memcpy(frame + ETH_HLEN, buf, len);
    size_t padding = 0;
    if (frame_length < ETHER_MIN_LEN) {
        logDebug("try to send a too small eth frame (len %d). apply padding.", frame_length);
        padding = ETHER_MIN_LEN - frame_length;
        memset(frame + ETH_HLEN + len, 0, padding);
    }
    
    memset(frame + ETH_HLEN + len, 0, ETHER_CRC_LEN); // we dont calc CRC yet.

    if (pcap_sendpacket(get_pcap_handle(id), (u_char*) frame, frame_length + padding) != 0) {
        logError("fail to send eth frame. dev_id=%d", id);
        return -1;
    }

    logDebug("a packet was sent to device %s, frame_len=%u, workload_len=%u", 
        get_device_name(id), frame_length + padding, len + padding);
    return 0; // 0 for success
}

static void frame_handler(
    u_char *_dev_id, // user-specific data, used as dev_id 
    const struct pcap_pkthdr *h, 
    const u_char * bytes) {

    if (h->caplen != h->len) {
        logWarning("recv strange frame. caplen=%u, len=%u", h->caplen, h->len);
    }

    struct ether_header *eth_header = (struct ether_header*) bytes;


    int dev_id = *(int*) _dev_id;
    logDebug("recv a eth frame, dev=%s, protocol=%d", 
        get_device_name(dev_id), ntohs(eth_header->ether_type));

    // PNX DV upd
    if (ntohs(eth_header->ether_type) == kRoutingProtocolCode) {
        if (distance_upd_handler(dev_id, (const char *)(bytes + ETH_HLEN), h->caplen - ETH_HLEN) != 0) {
            logError("upper layer fails to handle PNX DV update packet");
        }
    }

    // ARP
    if (ntohs(eth_header->ether_type) == ETHERTYPE_ARP) {
        if (ARPHandler(dev_id, (const char *) bytes) != 0) {
            logError("upper layer fails to handle ARP packet");
        }
    }

    // IP
    if (ntohs(eth_header->ether_type) == ETHERTYPE_IP) {
        if (ip_packet_handler(bytes + ETH_HLEN, h->caplen - ETH_HLEN) != 0) {
            logError("upper layer fails to handle IP packet");
        }
    }

    // * @param buf Pointer to the frame.
    // * @param len Length of the frame.
    // * @param id ID of the device (returned by ‘addDevice‘) receiving current frame.

    auto callback = recv_callback.load();
    if (callback)
        callback(bytes, h->caplen, dev_id);
}

static pcap_handler callback_wrapper = &frame_handler;

int set_frame_receive_callback(FrameReceiveCallback callback) {
    recv_callback.store(callback);
    return 0;
}

int recv_thread_go(int device_id) {
    std::thread recv = std::thread([device_id]() {
        pcap_t *dev = get_pcap_handle(device_id);
        while (1) {
            pcap_loop(dev, -1 /* infinity */, 
                callback_wrapper, (u_char*) &device_id);
            logWarning("device %d frame handler pcap_loop exit. try again...", device_id);
        }
    });
    recv.detach();
    return 0;
}
