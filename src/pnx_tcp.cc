#include <mutex>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <deque>
#include <unordered_map>
#include <assert.h>


#include "ringbuffer.h"
#include "tcp_segment.h"
#include "pnx_ip.h"
#include "pnx_tcp_tcb.h"
#include "pnx_tcp.h"
#include "pnx_socket.h"
#include "logger.h"
#include "pnx_utils.h"


using Seq = TCB::Sender::Sequence;

std::mutex tcp_lock;

struct SocketPair {
    struct sockaddr_in local;
    struct sockaddr_in remote;
};

std::unordered_map<SocketPair, TCB*> active_tcb_map;
std::unordered_map<uint16_t, SocketBlock*> listening_socket;

static int _tcp_timer(TCB *tcb) {
    std::lock_guard<std::mutex> lock(tcp_lock);

    if (tcb->state == TCP_CLOSE) {
        // do nothing
        return 0;
    }

    if (tcb->state == TCP_TIME_WAIT) {
        // wait 2MSL and close the connection.
        if (get_time_us() - tcb->send.last_sent_time >= 2 * kTcpMSL) {
            tcb->state = TCP_CLOSE;
        }
        return 0;
    }

    // check if the last segment is timeout.
    if (tcb->send.waiting_for_ack()) {
        if (get_time_us() - tcb->send.last_sent_time >= kTcpTimeout) {

            if (tcb->send.retrans_count >= kTcpMaxRetrans) {
                // close the connection.
                tcb->state = TCP_CLOSE;
                return 0;
            }

            // retransmit the last segment.
            tcb->send.last_sent_time = get_time_us();
            tcb->send.retrans_count++;
            if (ip_send_packet(tcb->send.last_seg.src, tcb->send.last_seg.dst, IPPROTO_TCP, tcb->send.last_seg.buf.get(), tcb->send.last_seg.len) != 0) {
                logWarning("tcp_timer: fail to retransmit the last segment");
                return -1;
            }
        }
    } else 
        tcb->send.retrans_count = 0;
    return 0;
}

static int _init_TCB(TCB* tcb, const sockaddr_in *local, const sockaddr_in *remote, std::shared_ptr<Segment> syn) {
    if (syn == nullptr) {
        // active open
        tcb->state = TCP_SYN_SENT;
        tcb->passive = false;
        tcb->local = *local;
        tcb->remote = *remote;

        { // init the sender part.
            tcb->send.init_seq = rand() % 10000;
            tcb->send.remote_recv_window = 0;
            tcb->send.next = tcb->send.init_seq;
            tcb->send.unack = tcb->send.init_seq;
            tcb->send.retrans_count = 0;
            tcb->send.last_sent_time = 0;
        }

        { // init the receiver part.
            // we dont know the receiver part for an active open.
            tcb->recv.init_seq = 0;
            tcb->recv.next = 0;
            tcb->recv.window = kTcpRecvBufferSize;
        }
    } else {
        assert(syn->hdr->syn == 1);

        tcb->state = TCP_SYN_RECV;
        tcb->passive = true;
        tcb->local = *local;
        tcb->remote = *remote;

        { // init the sender part.
            tcb->send.init_seq = rand() % 10000;
            tcb->send.remote_recv_window = syn->hdr->window;
            tcb->send.next = tcb->send.init_seq;
            tcb->send.unack = tcb->send.init_seq;
            tcb->send.retrans_count = 0;
            tcb->send.last_sent_time = 0;
        }

        { // init the receiver part.
            tcb->recv.init_seq = syn->hdr->seq;
            tcb->recv.next = syn->hdr->seq + 1; // init_recv_seq used by SYN
            tcb->recv.window = kTcpRecvBufferSize;
        }
    }

    tcb->timer = std::thread{[tcb]() {
        while (1) {
            // sleep 10 ms
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); 

            if (_tcp_timer(tcb) != 0) {
                logWarning("tcp_timer: fail");
            }
        }
    }};
}

static int _tcp_send_segment(TCB* tcb) {
    // construct a segment from tcb->send.buf.

    // when the buffer is empty, send a pure ACK, which does not require retransmission.
    // otherwise, send the first kTcpMaxSession bytes (at most), until a control seq.
    // if the first is a control signal, send it solely.

    assert(tcb->send.waiting_for_ack() == false);

    if (tcb->send.buf.empty()) {
        // send a pure ACK.

        Segment ack{sizeof(struct tcphdr)};
        ack.hdr->source = tcb->local.sin_port;
        ack.hdr->dest = tcb->remote.sin_port;
        ack.hdr->seq = tcb->send.next;
        ack.hdr->ack_seq = tcb->recv.next;
        ack.hdr->ack = 1;
        ack.hdr->window = tcb->recv.buf.rest_capacity();

        ack.src = tcb->local.sin_addr;
        ack.dst = tcb->remote.sin_addr;

        ack.fill_in_tcp_checksum();

        if (ip_send_packet(ack.src, ack.dst, IPPROTO_TCP, ack.buf.get(), ack.len) != 0) {
            logWarning("fail to send a pure ACK");
            return -1;
        }

        return 0;
    }

    // assert(tcb->send.state != TCB::Sender::WAIT_FOR_ACK);

    size_t payload_len = 0;

    char segment[kTcpMaxSegmentSize];
    struct tcphdr *hdr = (struct tcphdr*)segment;
    memset(hdr, 0, sizeof(struct tcphdr));
    hdr->source = tcb->local.sin_port;
    hdr->dest = tcb->remote.sin_port;
    hdr->seq = tcb->send.next;
    hdr->ack_seq = tcb->recv.next;
    hdr->doff = sizeof(struct tcphdr) / 4;

    // if a initial SYN is sent, then the ack bit is 0.
    // Otherwise we always send a ACK.
    hdr->ack = tcb->state == TCP_SYN_SENT ? 0 : 1;
    hdr->window = tcb->recv.buf.rest_capacity();
    
    if (tcb->send.buf.peek().value().isCtrl()) {
        payload_len = 0;
        hdr->fin = tcb->send.buf.peek().value().fin;
        hdr->syn = tcb->send.buf.peek().value().syn;
        tcb->send.buf.pop();
    } else {
        // get kTcpMaxSession bytes from the buffer, or until a control seq.
        while (payload_len + sizeof(tcphdr) < kTcpMaxSegmentSize && !tcb->send.buf.empty()) {
            Seq seq = tcb->send.buf.peek().value();
            if (seq.isCtrl()) break;
            segment[sizeof(tcphdr) + payload_len] = tcb->send.buf.pop()->byte;
            ++payload_len;
        }
    }
    
    Segment seg{segment, sizeof(struct tcphdr) + payload_len, tcb->local.sin_addr, tcb->remote.sin_addr};
    seg.fill_in_tcp_checksum();

    // tcb state update
    tcb->send.retrans_count = 0;
    tcb->send.last_sent_time = get_time_us();
    tcb->send.next += payload_len + seg.hdr->fin + seg.hdr->syn;
    tcb->send.last_seg = seg;

    if (ip_send_packet(seg.src, seg.dst, IPPROTO_TCP, seg.buf.get(), seg.len) != 0) {
        logWarning("fail to send a segment");
        return -1;
    }

    return 0;
}

static int _tcp_send_ctrl(TCB* tcb, Seq ctrl) {
    if (tcb->send.buf.push(ctrl) == 0) {
        logWarning("fail to send syn due to full buffer");
        return -1;
    }
    return _tcp_make_sure_sendback(tcb);
}

TCB* tcp_open(const sockaddr_in* local, const sockaddr_in* given_remote, std::shared_ptr<Segment> syn) {
    // if a SYN packet is given, then an active TCB is created.
    // otherwise a passive TCB is created, and jump to TCP_SYN_RECV state.

    std::lock_guard<std::mutex> lock(tcp_lock);

    sockaddr_in remote;
    if (given_remote != nullptr) {
        remote = *given_remote;
    } else {
        assert(syn != nullptr);
        remote.sin_addr = syn->src;
        remote.sin_port = syn->hdr->source;
    }

    SocketPair pair{*local, remote};

    if (active_tcb_map.find(pair) != active_tcb_map.end()) {
        logWarning("unimplemented tcp_open: already exists");
        return nullptr;
    }

    TCB* tcb = new TCB();
    active_tcb_map[pair] = tcb;

    _init_TCB(tcb, local, &remote, syn);

    if (syn != nullptr) {
        // passive open by a SYN, send back a SYN(ack)
        if (_tcp_send_ctrl(tcb, Seq{.syn = 1, .fin = 0, .byte = 0}) != 0) {
            logWarning("tcp_open: fail to send SYNACK");
        }
    } else {
        // active open, send a SYN without ack.
        if (_tcp_send_ctrl(tcb, Seq{.syn = 1, .fin = 0, .byte = 0}) != 0) {
            logWarning("tcp_open: fail to send SYN");
        }
    }

    return tcb;
}

bool tcp_register_listening_socket(SocketBlock *sb, uint16_t port /* network order */) {
    std::lock_guard<std::mutex> lock(tcp_lock);

    if (listening_socket.find(port) != listening_socket.end()) {
        logWarning("tcp_register_listening_socket: already exists");
        return false;
    }

    listening_socket[port] = sb;
    return true;
}

bool tcp_unregister_listening_socket(SocketBlock *sb, uint16_t port) {
    std::lock_guard<std::mutex> lock(tcp_lock);

    if (listening_socket.find(port) == listening_socket.end()) {
        logWarning("tcp_unregister_listening_socket: not found");
        return false;
    }
    if (listening_socket[port] != sb) {
        logWarning("tcp_unregister_listening_socket: not match");
        return false;
    }
    listening_socket.erase(port);
    return true;
}

int tcp_close(TCB *tcb) {
    std::lock_guard<std::mutex> lock(tcp_lock);
    switch (tcb->state) {
        TCP_CLOSE:
        TCP_FIN_WAIT1:
        TCP_FIN_WAIT2:
        TCP_CLOSING:
        TCP_LAST_ACK:
        TCP_TIME_WAIT:
            return 0;

        TCP_SYN_SENT:
        TCP_LISTEN:
            tcb->state = TCP_CLOSE;
            return 0;

        TCP_SYN_RECV:
        TCP_ESTABLISHED:
            tcb->state = TCP_FIN_WAIT1;
            return _tcp_send_ctrl(tcb, Seq{.syn = 0, .fin = 1, .byte = 0});

        TCP_CLOSE_WAIT:
            tcb->state = TCP_LAST_ACK;
            return _tcp_send_ctrl(tcb, Seq{.syn = 0, .fin = 1, .byte = 0});
    }
}

int tcp_send(TCB* tcb, const void *buf, int len) {
    std::lock_guard<std::mutex> lock(tcp_lock);
        
    // check state
    if (tcb->state != TCP_ESTABLISHED) {
        // for simplicity, we only support the simplest case.
        logWarning("tcp_send: not in ESTABLISHED state");
        return -1;
    }

    if (len == 0)
        return 0;

    // we reject all data if the buffer is full.
    if (tcb->send.buf.rest_capacity() < len) {
        logWarning("tcp_send: send buffer can not contain the data");
        return -1;
    }

    // push the data into the buffer.
    for (int i = 0; i < len; i++) {
        if (tcb->send.buf.push(Seq{.syn = 0, .fin = 0, .byte = ((char*)buf)[i]}) == 0) {
            logWarning("tcp_send: fail to push data into buffer");
            return -1;
        }
    }

    if (tcb->send.waiting_for_ack() == false) {
        if (_tcp_send_segment(tcb) != 0) {
            logWarning("tcp_send: fail to send segments");
            return -1;
        }
    }
    // Otherwise, the ack will trigger the next segment to be sent.
    return len;
}

int tcp_receive(TCB *tcb, void *buf, int len) {
    std::lock_guard<std::mutex> lock(tcp_lock);

    // we only care about recv.buf, no matter what state we are.

    int recv = 0;
    while (recv < len && !tcb->recv.buf.empty()) {
        ((char*)buf)[recv++] = tcb->recv.buf.pop().value();
    }
    return recv;
}

sockaddr_in tcp_getpeeraddress(TCB *tcb) { 
    std::lock_guard lock(tcp_lock);
    return tcb->remote;
}

static int _tcp_check_seq_consist(TCB *tcb, std::shared_ptr<Segment> seg) {
    return seg->hdr->seq == tcb->recv.next;
}

static int _tcp_make_sure_sendback(TCB *tcb) {
    // make sure there will be an event to send any update of tcb to the remote.
    if (tcb->send.waiting_for_ack() == false) {
        if (_tcp_send_segment(tcb) != 0) {
            logWarning("tcp_handle_segment_fin_wait1: fail to send pure ACK");
            return -1;
        }
    }
    return 0;
}

static int _tcp_handle_segment_syn_recv(TCB *tcb, std::shared_ptr<Segment> seg) {
    // handle pure ack only
    if (seg->len != sizeof(struct tcphdr) || seg->hdr->syn == 1 || seg->hdr->fin == 1) {
        logWarning("tcp_handle_segment_syn_recv: not an pure ack");
        return -1;
    }

    // check if the ack is valid.
    if (seg->hdr->ack_seq != tcb->send.init_seq) {
        logWarning("tcp_handle_segment_syn_recv: not acking my synack");
        return -1;
    }

    // check if the seq is valid.
    if (seg->hdr->seq != tcb->recv.init_seq + 1) {
        logWarning("tcp_handle_segment_syn_recv: strange seq");
        return -1;
    }

    // check if the window is valid.
    if (seg->hdr->window > kTcpRecvBufferSize) {
        logWarning("tcp_handle_segment_syn_recv: too large window for me");
        return -1;
    }

    if (seg->hdr->ack_seq > tcb->send.unack)
        tcb->send.unack = seg->hdr->ack_seq;

    tcb->state = TCP_ESTABLISHED;
    return 0;
}

static int _tcp_handle_segment_syn_sent(TCB *tcb, std::shared_ptr<Segment> seg) {
    // handle pure SYN ACK only
    if (seg->len != sizeof(tcphdr) || 
        seg->hdr->syn == 0 || seg->hdr->ack == 0 || seg->hdr->fin == 1) {

        logWarning("tcp_handle_segment_syn_sent: not a SYNACK");
        return -1;
    }

    // check if the ack is valid.
    if (seg->hdr->ack_seq != tcb->send.init_seq) {
        logWarning("tcp_handle_segment_syn_sent: not acking my syn");
        return -1;
    }

    // fill in remote info
    tcb->recv.init_seq = seg->hdr->seq;
    tcb->recv.next = seg->hdr->seq + 1; // init_recv_seq used by SYN
    tcb->send.remote_recv_window = seg->hdr->window;

    if (seg->hdr->ack_seq > tcb->send.unack)
        tcb->send.unack = seg->hdr->ack_seq;

    tcb->state = TCP_ESTABLISHED;
    _tcp_make_sure_sendback(tcb);
    return 0;
}

static int _tcp_handle_segment_established(TCB *tcb, std::shared_ptr<Segment> seg) {
    // normal or fin

    // handle ack update first
    if (seg->hdr->ack_seq > tcb->send.unack)
        tcb->send.unack = seg->hdr->ack_seq;

    if (tcb->recv.buf.rest_capacity() < seg->len - sizeof(struct tcphdr)) {
        logWarning("tcp_handle_segment_established: too much data to recv");
        return -1;
    }

    // push the data into the buffer.
    for (int i = sizeof(struct tcphdr); i < seg->len; i++) {
        if (tcb->recv.buf.push(((char*)seg->buf.get())[i]) == 0) {
            logWarning("tcp_handle_segment_established: fail to push data into buffer");
            return -1;
        }
        tcb->recv.next++;
    }

    // handle fin
    if (seg->hdr->fin == 1) {
        tcb->state = TCP_CLOSE_WAIT;
        tcb->recv.next++;
    }

    _tcp_make_sure_sendback(tcb);

    return 0;
}

static int _tcp_handle_segment_fin_wait1(TCB *tcb, std::shared_ptr<Segment> seg) {
    // we have close() the tcp conn. 
    // two possibility: 1. the remote FIN reached. 2. my FIN is acked, 

    // update ack
    if (seg->hdr->ack_seq > tcb->send.unack)
        tcb->send.unack = seg->hdr->ack_seq;

    if (seg->have_payload()) {
        logWarning("tcp_handle_segment_fin_wait1: not a pure segment");
        return -1;
    }

    if (seg->hdr->syn == 1) {
        logWarning("tcp_handle_segment_fin_wait1: strange SYN bit");
        return -1;
    }

    // case 1: with fin
    if (seg->hdr->fin == 1) {
        tcb->recv.next ++;
        _tcp_make_sure_sendback(tcb);
        return 0;
    }

    // case 2: without fin
    if (tcb->send.buf.empty() && tcb->send.waiting_for_ack() == false) {
        // my FIN is sent, and acked (by this segment).
        tcb->state = TCP_FIN_WAIT2;
        return 0;
    }
}

static int _tcp_handle_segment_fin_wait2(TCB *tcb, std::shared_ptr<Segment> seg) {
    if (seg->hdr->ack_seq > tcb->send.unack)
        tcb->send.unack = seg->hdr->ack_seq;

    
    // only pure FIN with ack can be received in this state.
    if (seg->have_payload() || seg->hdr->syn == 1 || seg->hdr->fin == 0) {
        logWarning("tcp_handle_segment_fin_wait2: not a pure FIN");
        return -1;
    }

    // a pure FIN with ack
    tcb->recv.next++;
    tcb->state = TCP_TIME_WAIT;
    _tcp_make_sure_sendback(tcb);
    return 0;
}

static int _tcp_handle_segment_close_wait(TCB *tcb, std::shared_ptr<Segment> seg) {
    // abandon this seg since we have received a FIN.
    logWarning("tcp_handle_segment_close_wait: recv segment in CLOSE_WAIT state");
    return -1;
}

static int _tcp_handle_segment_closing(TCB *tcb, std::shared_ptr<Segment> seg) {
    // only allow pure ACK of my FIN in this state

    if (seg->have_payload() || seg->hdr->syn == 1 || seg->hdr->fin == 1) {
        logWarning("tcp_handle_segment_closing: not a pure ACK");
        return -1;
    }

    if (seg->hdr->ack_seq != tcb->send.next) {
        logWarning("tcp_handle_segment_closing: not acking my FIN");
        return -1;
    }

    tcb->state = TCP_TIME_WAIT;
    return 0;
}

static int _tcp_handle_segment_last_ack(TCB *tcb, std::shared_ptr<Segment> seg) {
    // only allow pure ACK of my FIN in this state

    if (seg->have_payload() || seg->hdr->syn == 1 || seg->hdr->fin == 1) {
        logWarning("tcp_handle_segment_last_ack: not a pure ACK");
        return -1;
    }

    if (seg->hdr->ack_seq != tcb->send.next) {
        logWarning("tcp_handle_segment_last_ack: not acking my FIN");
        return -1;
    }

    tcb->state = TCP_CLOSE;
    return 0;
}

static int _tcp_handle_segment_time_wait(TCB *tcb, std::shared_ptr<Segment> seg) {
    // if we receive a FIN again, ack back again, which is done by determining tcp_segment_handler();
    return 0;
}

int tcp_segment_handler(const void* buf, int len, const struct in_addr& src, const struct in_addr& dst) {
    std::lock_guard<std::mutex> lock(tcp_lock);

    // construct a Segment
    std::shared_ptr<Segment> seg = std::make_shared<Segment>(buf, len, src);


    // the very first things is to check the checksum.
    auto ck = seg->hdr->check;
    seg->hdr->check = 0;
    seg->fill_in_tcp_checksum();
    if (seg->hdr->check != ck) {
        logWarning("tcp_segment_handler: checksum error");
        return -1;
    }

    SocketPair tuple4 = {0};
    tuple4.local.sin_addr = dst;
    tuple4.local.sin_port = seg->hdr->dest;
    tuple4.remote.sin_addr = src;
    tuple4.remote.sin_port = seg->hdr->source;

    // handle SYN, i.e. connection openning.
    if (seg->hdr->syn == 1 && seg->hdr->ack == 0) {
        // check if there is a listening socket
        if (listening_socket.find(seg->hdr->dest) == listening_socket.end()) {
            logWarning("tcp_segment_handler: no listening socket");
            return -1;
        }
        SocketBlock *sb = listening_socket[seg->hdr->dest];
        
        TCB *tcb = tcp_open(&tuple4.local, &tuple4.remote, seg);
        assert(tcb->state == TCP_SYN_RECV); // wait for the ack of syn.

        socket_recv_new_tcp_conn(sb, tcb);

        return 0;
    }

    // for other cases, there should be a specific socket to handle this segment.
    if (active_tcb_map.find(tuple4) == active_tcb_map.end()) {
        logWarning("tcp_segment_handler: no active socket can reponse to SYNACK");
        return -1;
    }
    TCB *tcb = active_tcb_map[tuple4];

    if (tcb->state == TCP_CLOSE) {
        logWarning("tcp_segment_handler: recv a segment when the connection closed");
        return -1;
    }

    // the very first things is to check the seq.
    // if the seq does not match, we abandon this segment and clarify our progress again. 
    // reasons: maybe last connection with the same tuple4, or outdated segment, or ACK loss.
    if (!_tcp_check_seq_consist(tcb, seg)) {
        // sync our progress to remote.
        // possibily an ACK loss.
        _tcp_make_sure_sendback(tcb);

        logWarning("tcp_handle_segment: seq not consistent, ack back again.");
        return -1;
    }

    // Note:
    // in my partial implementation, any segment contains control bits will not contain data.

    // handle other segments except the first SYN
    assert(seg->hdr->ack == 1);

    switch (tcb->state) {
        TCP_SYN_RECV:
            return _tcp_handle_segment_syn_recv(tcb, std::move(seg));
        TCP_SYN_SENT:
            return _tcp_handle_segment_syn_sent(tcb, std::move(seg));
        TCP_ESTABLISHED:
            return _tcp_handle_segment_established(tcb, std::move(seg));
        TCP_FIN_WAIT1:
            return _tcp_handle_segment_fin_wait1(tcb, std::move(seg));
        TCP_FIN_WAIT2:
            return _tcp_handle_segment_fin_wait2(tcb, std::move(seg));
        TCP_CLOSE_WAIT:
            return _tcp_handle_segment_close_wait(tcb, std::move(seg));
        TCP_CLOSING:
            return _tcp_handle_segment_closing(tcb, std::move(seg));
        TCP_LAST_ACK:
            return _tcp_handle_segment_last_ack(tcb, std::move(seg));
        TCP_TIME_WAIT:
            return _tcp_handle_segment_time_wait(tcb, std::move(seg));
        default:
            logWarning("tcp_segment_handler: invalid state");
            return -1;
    }
}


