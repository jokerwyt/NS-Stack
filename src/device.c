#include "device.h"
#include "common.h"
#include "packetio.h"

#include <sys/ioctl.h>
#include <net/if.h>
#include <errno.h>
#include <netinet/ether.h>

#define MAX_DEVICE_NUM 256

static atomic_int device_count = ATOMIC_VAR_INIT(0);
static char *dev_name[MAX_DEVICE_NUM];
static char dev_mac_addr[MAX_DEVICE_NUM][ETH_ALEN];
static pcap_t *dev[MAX_DEVICE_NUM];

int add_device(const char* device) {
    // ========== query mac address first ==========
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
    close(sockfd);
    memcpy(dev_mac_addr[device_count], ifr.ifr_hwaddr.sa_data, ETH_ALEN);

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
    // ========== fire recv thread ==========
    recv_thread_go(new_id);

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

const char* dev_mac(int id) {
    if (!is_valid_id(id)) {
        logError("try to get invalid device mac. id=%d", id);
        return NULL;
    }
    return dev_mac_addr[id];
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

    char **ret = malloc(sizeof(char*) * (*n));
    int i = 0;
    for (pcap_if_t *dev = alldevs; dev != NULL; dev = dev->next) {
        ret[i++] = strdup(dev->name);
    }

    pcap_freealldevs(alldevs);
    return ret;
}
