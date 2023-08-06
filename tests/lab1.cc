#include "device.h"
#include "packetio.h"
#include "logger.h"
#include "pnx_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

// the server works in NS1 and the client works in NS2.

int main(int argc, char **args) {
    // check if there is an target MAC to specify client and server.
    if (argc > 2) {
        logError("Usage for server: %s", args[0]);
        logError("Usage for client: %s <Target MAC addr>", args[0]);
        return 0;
    } else if (argc == 2) 
        pnx_logger_perfix = "client";
    else /* argc == 1 */
        pnx_logger_perfix = "server";

    int dcnt = 0;
    char **devices = get_host_device_lists(&dcnt);
    logInfo("Available devices:");
    for (int i = 0; i < dcnt; i++) {
        logInfo("%d: %s", i, devices[i]);
    }
    free(devices);
    if (argc == 1) {
        // server case
        pnx_logger_perfix = "server";

        const char * device = "veth1-2";
        int id = add_device(device);
        if (id < 0) {
            logError("fail to add device %s", device);
            return -1;
        }

        // set callback
        logInfo("server started. waiting for frame...");

        auto frame_receive_callback = [](const void* workload, int len, int id) {
            (void)workload; // eliminate GCC warn
            logInfo("Frame received. len=%d, device=%s, content=%s", len, get_device_name(id), (char*) workload);
            return 1;
        };

        set_frame_receive_callback(frame_receive_callback);
        sleep(10086);
    } else {
        // client case
        pnx_logger_perfix = "client";

        const char * device = "veth2-1";
        int id = add_device(device);
        if (id < 0) {
            logError("fail to add device %s", device);
            return -1;
        }

        // keep injecting frame of the given target MAC
        char *target_mac_str = args[1];
        struct ether_addr target_mac;
        // cast target_mac to ether_addr
        if (ether_aton_r(target_mac_str, &target_mac) == nullptr) {
            logError("Invalid MAC address format");
            return -1;
        }

        //translate mac str into 6-byte array
        logInfo("target MAC: %s\n", target_mac_str);

        // make the frame larger than the minimum length.
        while (1) {
            long long time = get_time_us();
            char msg[1000];
            sprintf(msg, "hello world! %lld", time);
            send_frame(msg, strlen(msg), ETHERTYPE_IP, &target_mac, id);
            sleep(1);
        }
    }
}