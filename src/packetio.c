#include "common.h"
#include "packetio.h"
#include "device.h"

#include <pcap.h>
#include <pthread.h>
#include <stdatomic.h>

// it is actually a FrameReceiveCallback.
// I dont want to use mutex, for simplicity.
#define NONE 0
static atomic_size_t recv_callback = ATOMIC_VAR_INIT(NONE);

int send_frame(const void* buf, int len, int ethtype, const void* destmac, int id) {
    static char frame[ETHER_MAX_LEN];

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
    memcpy(eth_header->ether_dhost, destmac, ETH_ALEN);
    eth_header->ether_type = ethtype;

    memcpy(frame + ETH_HLEN, buf, len);
    size_t padding = 0;
    if (frame_length < ETHER_MIN_LEN) {
        logWarning("try to send a too small eth frame (len %d). apply padding.", frame_length);
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



void callback_wrapper_(
    u_char *user, // user-specific data, used as dev_id 
    const struct pcap_pkthdr *h, 
    const u_char * bytes) {

    int dev_id = -1;
    NO_WARN_CAST(int, dev_id, user);

    // get the work load
    const u_char *workload = bytes + ETH_HLEN;

    // * @param buf Pointer to the frame.
    // * @param len Length of the frame.
    // * @param id ID of the device (returned by ‘addDevice‘) receiving current frame.
    size_t loadout = atomic_load(&recv_callback);
    if (loadout != NONE)
        ((FrameReceiveCallback)(loadout))(workload, h->caplen - ETH_HLEN - ETHER_CRC_LEN, dev_id);
}

pcap_handler callback_wrapper = &callback_wrapper_;

static void* frame_handler_thread(void* dev_id__) {
    int dev_id = 0;
    NO_WARN_CAST(int, dev_id, dev_id__);

    pcap_t *dev = get_pcap_handle(dev_id);
    while (1) {
        pcap_loop(dev, -1 /* infinity */, 
            callback_wrapper, dev_id__);
        logWarning("device %d frame handler pcap_loop exit. try again...", dev_id);
    }
    return NULL;
}


int set_frame_receive_callback(FrameReceiveCallback callback) {
    atomic_store(&recv_callback, (size_t) callback);
    return 0;
}

int recv_thread_go(int device_id) {
    pthread_t thr;
    void * user_specific = NULL;
    NO_WARN_CAST(void*, user_specific, device_id);

    if (pthread_create(&thr, NULL, frame_handler_thread, user_specific) != 0) {
        logFatal("create recv thread fail. device_id=%d", device_id);
        exit(-1);
    }
    return 0;
}
