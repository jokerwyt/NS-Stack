#include "device.h"
#include "packetio.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


int frame_receive_callback(const void* buf, int len, int id) {
    printf("frame received. len=%d, device id=%d\n", len, id);
    // print content
    for (int i = 0; i < len; i++) {
        printf("%02x ", ((unsigned char*) buf)[i]);
    }
    return 1;
}
FrameReceiveCallback callback = &frame_receive_callback;

int main(int argc, char **args) {
    // print all available devices
    int dcnt = 0;
    char **devices = get_host_device_lists(&dcnt);
    printf("Available devices:\n");
    for (int i = 0; i < dcnt; i++) {
        printf("%d: %s\n", i, devices[i]);
    }

    // check if there is an target MAC to specify client and server.
    if (argc > 2) {
        printf("Usage for server: %s\n", args[0]);
        printf("Usage for client: %s <Target MAC addr>\n", args[0]);
        return 0;
    }

    printf("use the first device: %s\n", devices[0]);
    int id = add_device(devices[0]);
    if (id < 0) {
        printf("fail to add device %s\n", devices[0]);
        return -1;
    }
    printf("device %s added, id=%d\n", devices[0], id);
    free(devices);

    // print device MAC
    const char *mac = dev_mac(id);
    // translate 6-byte mac into string
    char mac_str[18];
    sprintf(mac_str, "%02x:%02x:%02x:%02x:%02x:%02x", 
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    printf("device %d MAC: %s\n", id, mac_str);

    if (argc == 1) {
        // server case
        // set callback
        set_frame_receive_callback(callback);

        printf("server started. waiting for frame... exits after 60 sec\n");
        sleep(60);
    } else {
        // client case
        // inject frame of the given target MAC
        char *target_mac = args[1];
        unsigned char target_mac_bytes[6];

        //translate mac str into 6-byte array
        sscanf(target_mac, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx", 
            &target_mac_bytes[0], &target_mac_bytes[1], &target_mac_bytes[2], 
            &target_mac_bytes[3], &target_mac_bytes[4], &target_mac_bytes[5]);
        printf("target MAC: %s\n", target_mac);
        const char * msg = "hello hello hello hello hello hello hello hello hello hello";
        send_frame(msg, strlen(msg), ETHERTYPE_IP, (const void*)target_mac_bytes, id);
    }
}