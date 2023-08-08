#include "device.h"
#include "common.h"
#include "packetio.h"
#include "routing.h"

#include <sys/ioctl.h>
#include <net/if.h>
#include <errno.h>
#include <netinet/ether.h>
#include <mutex>

#define MAX_DEVICE_NUM 256

// it may share with multiple threads.
static std::atomic<int> device_count{0}; 

// the following data is read-only. 
static char *dev_name[MAX_DEVICE_NUM];
static struct ether_addr dev_mac_addr[MAX_DEVICE_NUM];
static struct in_addr dev_ip_addr[MAX_DEVICE_NUM];
static struct in_addr dev_mask_addr[MAX_DEVICE_NUM];
static pcap_t *dev[MAX_DEVICE_NUM];

int add_device(const char* device) {
    static std::once_flag flag;
    std::call_once(flag, []() {
        // ========== fire routing upd timer ==========
        fire_distance_upd_daemon();
    });

    // ========== pcap open it ==========
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_t *handle = pcap_open_live(device, BUFSIZ, 
        1, /* promisc mode */
        1000, /* wait time */
        errbuf);
    
    if (handle == NULL) {
        logError("fail pcap_open_live: %s", errbuf);
        return -1;
    }

    int new_id = atomic_fetch_add(&device_count, 1);

    dev[new_id] = handle;
    dev_name[new_id] = strdup(device);

    // ========== query mac address  ==========
    struct ifreq ifr;
    int sockfd;
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        logError("create socket fail. errmsg=%s", strerror(errno));
        return -1;
    }
    strncpy(ifr.ifr_name, device, IFNAMSIZ);
    if (ioctl(sockfd, SIOCGIFHWADDR, &ifr) < 0) {
        logError("can not get device mac addr. errmsg=%s", strerror(errno));
        close(sockfd);
        return -1;
    }
    memcpy(dev_mac_addr[new_id].ether_addr_octet, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

    // ========== query ip ==========
    if (ioctl(sockfd, SIOCGIFADDR, &ifr) < 0) {
        logError("can not get device ip addr. errmsg=%s", strerror(errno));
        close(sockfd);
        return -1;
    }
    memcpy(&dev_ip_addr[new_id], &((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr, sizeof(in_addr));

    // ========== query subnet mask ==========
    if (ioctl(sockfd, SIOCGIFNETMASK, &ifr) < 0) {
        logError("can not get device subnet mask. errmsg=%s", strerror(errno));
        close(sockfd);
        return -1;
    }
    memcpy(&dev_mask_addr[new_id], &((struct sockaddr_in *)&ifr.ifr_netmask)->sin_addr, sizeof(in_addr));
    close(sockfd);


    // ========== fire recv thread ==========
    recv_thread_go(new_id);

    char buf[PNX_MAC_STR_LEN];
    logInfo("added device %s, id=%d, MAC=%s, IP=%s, subnet_mask=%s", device, 
        new_id, mac_to_str(dev_mac_addr[new_id].ether_addr_octet, buf), 
        inet_ntoa_safe(dev_ip_addr[new_id]).get(),
        inet_ntoa_safe(dev_mask_addr[new_id]).get());


    // ========== add to routing table ==========
    add_static_routing_entry(dev_ip_addr[new_id], dev_mask_addr[new_id], 
        dev_ip_addr[new_id], device, true /* direct neighbour */);

    return new_id;
}

int find_device(const char* device) {
    int dcnt = atomic_load(&device_count);
    for (int id = 0; id < dcnt; id++) {
        if (strcmp(device, dev_name[id]) == 0) {
            return id;
        }
    }
    logWarning("fail findDevice: %s", device);
    return -1;
}

const struct ether_addr *dev_mac(int id) {
    if (!is_valid_id(id)) {
        logError("try to get invalid device mac. id=%d", id);
        return NULL;
    }
    return &dev_mac_addr[id];
}

const in_addr *dev_ip(int id) {
    if (!is_valid_id(id)) {
        logError("try to get invalid device ip. id=%d", id);
        return nullptr;
    }
    return &dev_ip_addr[id];
}

const in_addr *dev_mask(int id) {
    if (!is_valid_id(id)) {
        logError("try to get invalid device mask. id=%d", id);
        return nullptr;
    }
    return &dev_mask_addr[id];
}

int is_valid_id(int id) {
    return 0 <= id && id < atomic_load(&device_count);
}

pcap_t* get_pcap_handle(int id) {
    if (!is_valid_id(id)) {
        logError("try to get invalid device pcap handle. id=%d", id);
        return NULL;
    }
    return dev[id];
}

char ** get_host_device_lists(int *n) {
    pcap_if_t *alldevs;
    char errbuf[PCAP_ERRBUF_SIZE];
    
    *n = 0;
    if (pcap_findalldevs(&alldevs, errbuf) == -1) {
        logError("Error finding devices: %s\n", errbuf);
        return NULL;
    }

    for (pcap_if_t *dev = alldevs; dev != NULL; dev = dev->next) {
        (*n)++;        
    }

    char **ret = (char**) malloc(sizeof(char*) * (*n));
    int i = 0;
    for (pcap_if_t *dev = alldevs; dev != NULL; dev = dev->next) {
        ret[i++] = strdup(dev->name);
    }

    pcap_freealldevs(alldevs);
    return ret;
}

const char *get_device_name(int id) { 
    if (!is_valid_id(id)) {
        return NULL;
    }
    return dev_name[id];
}

int get_dev_from_subnet(const in_addr ip, const in_addr mask) {
    int dcnt = atomic_load(&device_count);
    for (int id = 0; id < dcnt; id++) {
        if (subnet_match(ip, dev_ip_addr[id], mask) 
            && mask.s_addr == dev_mask_addr[id].s_addr) {
            return id;
        }
    }
    logDebug("fail get_dev_from_subnet: ip=%s, mask=%s", 
        inet_ntoa_safe(ip).get(), inet_ntoa_safe(mask).get());
    return -1;
}

int get_dev_cnt() {
    return atomic_load(&device_count);
}
