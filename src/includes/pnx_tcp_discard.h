#pragma once

// buf points to the whole TCP segment (include TCP header).
int tcp_segment_handler(const void* buf, int len);

#include <mutex>
#include <netinet/tcp.h>
#include <deque>

#include "ringbuffer.h"
#include "tcp_segment.h"

const size_t kTcpSendBufferSize = 4096;
const size_t kTcpRecvBufferSize = 4096;

struct SocketPair {
    struct sockaddr_in local;
    struct sockaddr_in remote;
};

// a TCB represents an endpoint of a TCP connection.
struct TCB {
    // things access the TCB:
    // 1. timer thread, 
    // 2. tcp_segment_handler, 
    // 3. user thread.
    std::mutex lock; // protect the whole class
    
    int state;
    bool passive;

    sockaddr_in local; // in network byte order
    sockaddr_in remote; // in network byte order

    // ============= send part ================
    uint32_t init_send_seq;
    uint32_t remote_recv_window;
    uint32_t send_next;  // next to send
    uint32_t send_unack; // the oldest one that is not acked

    // segment sequence number used for last window update
    // uint32_t send_wl1;

    // segment acknowledgment number used for last window update
    // uint32_t send_wl2;

    RingBuffer<char, kTcpSendBufferSize> send_buf;

    struct SegmentRecord {
        Segment seg;
        size_t sent_time;
    };

    std::deque<SegmentRecord> sent_segment_seqs_;

    // when the user calls close(), we send a FIN packet.
    // if the send buffer is empty, we can send a FIN packet immediately.
    // Otherwise, we flush the send buffer first, and piggyback a FIN in the last packet.
    bool FIN_sent = false;

/*
  Send Sequence Space

                   1         2          3          4
              ----------|----------|----------|----------
                     SND.UNA    SND.NXT    SND.UNA
                                          +SND.WND

        1 - old sequence numbers which have been acknowledged
        2 - sequence numbers of unacknowledged data
        3 - sequence numbers allowed for new data transmission
        4 - future sequence numbers which are not yet allowed
*/



    // ============= recv part ================
    uint32_t init_recv_seq;
    uint32_t recv_window_size;
    uint32_t recv_next;

/*
  Receive Sequence Space

                       1          2          3
                   ----------|----------|----------
                          RCV.NXT    RCV.NXT
                                    +RCV.WND

        1 - old sequence numbers which have been acknowledged
        2 - sequence numbers allowed for new reception
        3 - future sequence numbers which are not yet allowed
*/
    RingBuffer<char, kTcpRecvBufferSize> recv_buf;


private:
    // do not call this function directly. use tcp_open() instead.
    TCB(const struct sockaddr_in *local, const struct sockaddr_in *remote, bool passive);
    friend TCB* tcp_open(const sockaddr_in* local, const sockaddr_in* remote, bool passive);

    std::thread timer;
    std::atomic<bool> timer_stop = false;

public:

    // start timer, put into tcb_records.
    void init();
    ~TCB();

public:
    // user calls.
    int send(void *buf, size_t len);

    // block until `len` bytes are received.
    int receive(void *buf, int len);

    int close();
    int abort();
    int status();

public:
    // internal event handling.
    int segment_arrive(std::unique_ptr<Segment> seg);
    int retrans_timeout();
    int time_wait_timeout();

private:
    int segment_arrive_syn_listen(std::unique_ptr<Segment> seg);
    int segment_arrive_syn_sent(std::unique_ptr<Segment> seg);
    int segment_arrive_syn_recv(std::unique_ptr<Segment> seg);
    int segment_arrive_syn_established(std::unique_ptr<Segment> seg);
};

// add to the records. 
TCB* tcp_open(const sockaddr_in* local, const sockaddr_in* remote, bool passive);
