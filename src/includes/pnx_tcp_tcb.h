#include <netinet/tcp.h>
#include <thread>
#include <netinet/in.h>

#include "pnx_tcp_const.h"
#include "ringbuffer.h"

struct TCB {
    int state;
    bool passive;

    sockaddr_in local; // in network byte order
    sockaddr_in remote; // in network byte order

    struct Sender {
        uint32_t init_seq;
        uint32_t remote_recv_window;
        uint32_t next;  // next to send
        uint32_t unack; // the oldest one that is not acked
        RingBuffer<char, kTcpSendBufferSize> buf;

        // when the user calls close(), we send a FIN packet.
        // if the send buffer is empty, we can send a FIN packet immediately.
        // Otherwise, we flush the send buffer first, and piggyback a FIN in the last packet.
        bool FIN_waiting;

        // when send.unack advance, clear this count.
        int retrans_count;

        // the timer will check this value to see if we need to retransmit.
        size_t last_sent_time;
    } send;

    struct Receiver {
        uint32_t init_seq;
        uint32_t next; // next to receive
        uint32_t window; // receive window
        RingBuffer<char, kTcpRecvBufferSize> buf;
    } recv;

    std::thread timer;
};

int init_TCB(TCB* tcb, const sockaddr_in* local, const sockaddr_in* remote, bool passive);