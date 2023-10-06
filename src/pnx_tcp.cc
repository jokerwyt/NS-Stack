#include <mutex>
#include <netinet/tcp.h>
#include <deque>
#include <unordered_map>


#include "pnx_tcp.h"
#include "ringbuffer.h"
#include "tcp_segment.h"
#include "pnx_tcp_tcb.h"
#include "socket.h"

std::mutex tcp_lock;

struct SocketPair {
    struct sockaddr_in local;
    struct sockaddr_in remote;
};

std::unordered_map<SocketPair, TCB*> active_tcb_map;
std::unordered_map<uint16_t, SocketBlock*> listening_socket;


TCB* tcp_open(const sockaddr_in* local, const sockaddr_in* remote, bool passive) {
    std::lock_guard<std::mutex> lock(tcp_lock);

    SocketPair pair{*local, *remote};

    if (tcb_map.find(pair) != tcb_map.end()) {
        return nullptr;
    }

    TCB* tcb = new TCB;
    tcb_map[pair] = tcb;

    init_TCB(tcb, local, remote, passive);

    return tcb;
}
