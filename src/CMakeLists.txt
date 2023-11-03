add_library(Pnx STATIC
    device.cc
    packetio.cc
    pnx_ip.cc
    arp.cc
    routing.cc
    logger.cc
    pnx_utils.cc
    pnx_socket.cc
    pnx_tcp.cc
    gracefully_shutdown.cc
)