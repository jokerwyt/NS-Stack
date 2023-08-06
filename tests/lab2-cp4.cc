#include "device.h"
#include "packetio.h"
#include "logger.h"
#include "pnx_utils.h"
#include "pnx_ip.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


// the client works in NS1 and the server works in NS4.
// the client sends IP packet to the server.

int main(int argc, char **args) {
    if (argc > 2) {
        logError("Usage for server: %s", args[0]);
        logError("Usage for client: %s <Dest IP addr>", args[0]);
        return 0;
    } else if (argc == 2) 
        pnx_logger_perfix = "client";
    else /* argc == 1 */
        pnx_logger_perfix = "server";

    if (argc == 1) {
        // server case
        pnx_logger_perfix = "server";

        const char * device = "veth4-3";
        int id = add_device(device);
        if (id < 0) {
            logError("fail to add device %s", device);
            return -1;
        }

        // set callback
        logInfo("server started. waiting for IP packet...");
        sleep(10086);
    } else {
        // client case
        pnx_logger_perfix = "client";

        const char * device = "veth1-2";
        int id = add_device(device);
        if (id < 0) {
            logError("fail to add device %s", device);
            return -1;
        }

        logInfo("target IP: %s", args[1]);
        
        // check the IP is valid and cast it into in_addr
        in_addr target_ip;
        if (inet_aton(args[1], &target_ip) == 0) {
            logError("invalid IP address: %s", args[1]);
            return -1;
        }

        while (1) {
            long long time = get_time_us();
            char msg[1000];
            sprintf(msg, "hello world! %lld", time);
            sendIPPacket(*dev_ip(id), target_ip, 0x0, msg, strlen(msg));
            sleep(1);
        }
    }
}