#include <netinet/tcp.h>
#include <thread>
#include <netinet/in.h>
#include <deque>

#include "pnx_tcp_const.h"
#include "ringbuffer.h"


/* 

# Life cycle of TCBs:
## start:
- active open: tcp_open() creates a TCB and returns it to the user.
- passive open: recv SYN, create a TCB and give it to the listening socket.

## ending:
- Once the user calls tcp_close(), the TCB becomes an orphan. Remove it from the TCB map.
- We periodically will check all orphan TCBs and delete those have been CLOSED.

*/

struct TCB {
    int state;
    bool passive;

    sockaddr_in local; // in network byte order
    sockaddr_in remote; // in network byte order

    struct Sender {
        uint32_t init_seq;
        uint32_t remote_recv_window;
        uint32_t next;  // next seq to send
        uint32_t unack; // the oldest one that is not ack by the remote. i.e. updated by the ack_seq.
        
        struct Sequence {
            bool syn, fin;
            // only make sense when both syn and fin is 0
            char byte;

            bool isCtrl() {
                return syn != 0 || fin != 0;
            }
        };
        // TODO: this design hinders direct memory copy. move control bits out of sender buffer.
        RingBuffer<Sequence, kTcpSendBufferSize> buf;

        Segment last_seg;

        // when the user calls close(), we send a FIN packet.
        // if the send buffer is empty, we can send a FIN packet immediately.
        // Otherwise, we flush the send buffer first, and piggyback a FIN in the last packet.
        // bool FIN_waiting;

        // when a new seg is sent, this value is set to 0.
        int retrans_count;

        // the timer will check this value to see if we need to retransmit.
        // only counts for data packets.
        size_t last_sent_time;

        inline bool waiting_for_ack() {
            return unack < next;
        }
    } send;

    struct Receiver {
        uint32_t init_seq;
        uint32_t next; // next seq to receive from the remote
        uint32_t window; // receive window
        RingBuffer<char, kTcpRecvBufferSize> buf;
    } recv;

    std::atomic<bool> timer_stop = false;
    std::thread timer;
};