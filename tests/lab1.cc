#include "device.h"
#include "packetio.h"
#include "logger.h"
#include "pnx_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

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
        char *target_mac = args[1];
        unsigned char target_mac_bytes[6];
        str_to_mac(target_mac, target_mac_bytes);

        //translate mac str into 6-byte array
        logInfo("target MAC: %s\n", target_mac);

        // make the frame larger than the minimum length.
        while (1) {
            long long time = get_time_us();
            char msg[1000];
            sprintf(msg, "hello world! %lld", time);
            send_frame(msg, strlen(msg), ETHERTYPE_IP, (const void*)target_mac_bytes, id);
            sleep(1);
        }
    }
}