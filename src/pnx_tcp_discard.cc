#include <assert.h>
#include <atomic>
#include <memory>
#include <string.h>
#include <vector>
#include <netinet/tcp.h>
#include <unordered_set>
#include <thread>

#include "logger.h"
#include "pnx_utils.h"
#include "rustex.h"

#include "pnx_ip.h"
#include "pnx_tcp.h"
#include "socket.h"

rustex::mutex<std::unordered_map<SocketPair, TCB*>> tcb_records;

static uint16_t tcp_calc_checksum(void *whole_segment, size_t segment_len, uint32_t src_ip, uint32_t dst_ip) {
    // calc the checksum of the ip header using the RFC specified algorithm.
    // https://tools.ietf.org/html/rfc1071

    // first set the checksum field to 0
    auto old_check = ((struct tcphdr*)whole_segment)->check;

    ((struct tcphdr*)whole_segment)->check = 0;

    /* Compute Internet Checksum for "count" bytes
    *         beginning at location "addr".
    */
    uint32_t sum = 0;

    int count = segment_len;
    uint16_t * addr = (uint16_t *) whole_segment;
    while( count > 1 )  {
        /*  This is the inner loop */
        sum += * (uint16_t*) addr++;
        count -= 2;
    }

    /*  Add left-over byte, if any */
    if ( count > 0 )
        sum += * (unsigned char *) addr;

    // add pseudo header
    sum += src_ip >> 16;
    sum += src_ip & 0xffff;
    sum += dst_ip >> 16;
    sum += dst_ip & 0xffff;
    sum += htons(IPPROTO_TCP);
    sum += htons(segment_len);

    /*  Fold 32-bit sum to 16 bits */
    while (sum>>16)
        sum = (sum & 0xffff) + (sum >> 16);

    ((struct tcphdr*)whole_segment)->check = old_check;
    return ~(uint16_t)sum;
}

TCB::TCB(const struct sockaddr_in *local, const struct sockaddr_in *remote, bool passive) {
    this->local = *local;
    if (remote != nullptr)
        this->remote = *remote;
    else
        memset(&this->remote, 0, sizeof(this->remote));

    this->passive = passive;
    state = passive ? TCP_LISTEN : TCP_CLOSE;
}

void TCB::init() {
    this->timer = std::thread([this](){
        while (this->timer_stop.load() == false) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            // TODO: timer things.
        }
    });
    tcb_records.lock_mut()->insert({{local, remote}, this});
}

TCB::~TCB() {
    logFatal("~TCB() unimplemented.");
    exit(-1);
    // timer_stop.store(true);
    // timer.join();
}

int TCB::receive(void *buf, int len) {
    std::lock_guard<std::mutex> lock(this->lock);

    if (state != TCP_ESTABLISHED) {
        logError("TCB::receive: state is not TCP_ESTABLISHED.");
        return -1;
    }
    // TODO:
}

int TCB::segment_arrive(void *buf, int len, const struct sockaddr_in *from) {
    std::lock_guard<std::mutex> lock(this->lock);

    struct tcphdr *tcp_header = (struct tcphdr*)buf;

    // check remote is null or match
    if (this->state == TCP_LISTEN && this->passive) {
        // if passive, we fill the remote address.
        remote = *from;
    } else {
        // if not passive, the address must match since we route segment by it.
    }

    // we first verify the checksum
    if (tcp_calc_checksum(buf, len, remote.sin_addr.s_addr, local.sin_addr.s_addr) != 0) {
        logError("TCB::segment_arrive: checksum error.");
        return -1;
    }

    // for different states, we do different things.
    switch (this->state) {
        TCP_CLOSE:
            logWarning("TCB::segment_arrive: segment received in TCP_CLOSE state.");
            return -1;
        TCP_LISTEN:
            // if ack.
            if (tcp_header->ack) {
                logWarning("TCB::segment_arrive: ACK received in TCP_LISTEN state.");
                return -1;
            } else 
            // if SYN
            if (tcp_header->syn) {
                this->state = TCP_SYN_RECV;
                this->init_recv_seq = ntohl(tcp_header->seq);
                this->recv_next = this->init_recv_seq + 1;
                this->recv_window_size = ntohs(tcp_header->window);

                this->init_send_seq = rand();
                this->send_next = this->init_send_seq + 1;
                this->send_unack = this->init_send_seq;

                // send SYN/ACK
                char buf[20];
                memset(buf, 0, sizeof(buf));
                struct tcphdr *tcp_header = (struct tcphdr*)buf;
                tcp_header->source = local.sin_port;
                tcp_header->dest = remote.sin_port;
                tcp_header->seq = htonl(this->init_send_seq);
                tcp_header->ack_seq = htonl(this->recv_next);
                tcp_header->doff = 5;
                tcp_header->syn = 1;
                tcp_header->ack = 1;
                tcp_header->window = htons(kTcpRecvBufferSize);
                tcp_header->check = 0;
                tcp_header->urg_ptr = 0;
                tcp_header->check = tcp_calc_checksum(buf, sizeof(buf), local.sin_addr.s_addr, remote.sin_addr.s_addr);
                if (ip_send_packet(local.sin_addr, remote.sin_addr, IPPROTO_TCP, buf, sizeof(buf)) == -1) {
                    logError("TCB::segment_arrive: fail to send SYN/ACK packet.");
                    state = TCP_CLOSE;
                    return -1;
                }
            } else {
                logWarning("TCB::segment_arrive: segment received in TCP_LISTEN state, but not SYN.");
                return -1;
            }
            break;
        TCP_SYN_SENT:
            return this->segment_arrive_syn_sent(buf, len, from);
        TCP_SYN_RECV:
            return this->segment_arrive_syn_recv(buf, len, from);
    }
}

int TCB::segment_arrive_syn_sent(void *buf, int len, const sockaddr_in *from) {
  return 0;
}

TCB* tcp_open(const sockaddr_in* local, const sockaddr_in* remote, bool passive) {
    auto tcb = new TCB(local, remote, passive);

    if (!passive) {
        // send SYN
        tcb->state = TCP_SYN_SENT; // from CLOSED to SYN_SENT
        tcb->init_send_seq = rand();
        tcb->send_next = tcb->init_send_seq;
        tcb->send_unack = tcb->init_send_seq;

        // construct a SYN header and pass it to ip layer.

        char buf[20];
        memset(buf, 0, sizeof(buf));

        auto tcp_header = (struct tcphdr*)buf;
        tcp_header->source = local->sin_port;
        tcp_header->dest = remote->sin_port;
        tcp_header->seq = htonl(tcb->init_send_seq);
        tcp_header->ack_seq = 0;
        tcp_header->doff = 5;
        tcp_header->syn = 1;
        tcp_header->window = htons(kTcpRecvBufferSize);
        tcp_header->check = 0;
        tcp_header->urg_ptr = 0;
        
        tcp_header->check = tcp_calc_checksum(buf, sizeof(buf), local->sin_addr.s_addr, remote->sin_addr.s_addr);

        // send the SYN packet
        if (ip_send_packet(local->sin_addr, remote->sin_addr, IPPROTO_TCP, buf, sizeof(buf)) == -1) {
            logError("tcp_open: fail to send SYN packet.");
            delete tcb;
            return nullptr;
        }
    }
    return tcb;
}
