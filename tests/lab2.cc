#include "device.h"
#include "packetio.h"
#include "logger.h"
#include "pnx_utils.h"
#include "pnx_ip.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


int main(int argc, char **args) {
    if (argc > 3) {
        logError("Usage for server: %s -ddev1,dev2...", args[0]);
        logError("Usage for client: %s <Dest IP addr> -ddev1,dev2...", args[0]);
        logError("The client will use dev1 to send packets.");
        return 0;
    }

    std::vector<std::string> devices;
    std::string target_ip;
    struct in_addr ip;
    for (int i = 1; i < argc; i++) {
        int len = strlen(args[i]);
        if (args[i][0] == '-') {
            if (len == 1) {
                logError("invalid argument: %s", args[i]);
                return -1;
            }
            if (args[i][1] == 'd') {
                // parse device list
                char *devs = args[i] + 2;
                char *dev = strtok(devs, ",");
                while (dev != NULL) {
                    devices.push_back(dev);
                    dev = strtok(NULL, ",");
                }
            }
        } else {
            // parse target IP
            target_ip = args[i];
            // validate it
            if (inet_aton(target_ip.c_str(), &ip) == 0) {
                logError("invalid IP address: %s", target_ip.c_str());
                return -1;
            }
        }
    }

    // add all devices
    for (auto &device : devices) {
        int id = add_device(device.c_str());
        if (id < 0) {
            logError("fail to add device %s", device.c_str());
            return -1;
        }
    }

    if (argc == 2) {
        // server case
        logInfo("server started.");
        sleep(10086);
    } else {
        // client case
        logInfo("target IP: %s", target_ip.c_str());

        while (1) {
            long long time = get_time_us();
            char msg[1000];
            sprintf(msg, "hello world! %lld", time);
            sendIPPacket(*dev_ip(0), ip, 0x0, msg, strlen(msg));
            sleep(1);
        }
    }
}