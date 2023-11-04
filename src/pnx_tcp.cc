#include <mutex>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <assert.h>


#include "ringbuffer.h"
#include "tcp_segment.h"
#include "pnx_ip.h"
#include "pnx_tcp_tcb.h"
#include "pnx_tcp.h"
#include "pnx_socket.h"
#include "logger.h"
#include "pnx_utils.h"
#include "gracefully_shutdown.h"
#include "rustex.h"


using Seq = TCB::Sender::Sequence;

static std::mutex tcp_lock;
static BlockingRingBuffer<TCB*, 100> orphaned_tcb;
static std::thread tcb_recycler;
static std::atomic<bool> tcb_recycler_can_stop{false};

struct SocketPair {
    struct sockaddr_in local;
    struct sockaddr_in remote;
    bool operator==(const SocketPair& other) const {
        // only compare the ip and port.
        return local.sin_addr.s_addr == other.local.sin_addr.s_addr
            && remote.sin_addr.s_addr == other.remote.sin_addr.s_addr
            && local.sin_port == other.local.sin_port
            && remote.sin_port == other.remote.sin_port;
    }
};

struct SocketPairHash {
    std::size_t operator()(const SocketPair& key) const {
        return std::hash<uint32_t>()(key.local.sin_addr.s_addr) ^ std::hash<uint32_t>()(key.remote.sin_addr.s_addr)
            ^ std::hash<uint16_t>()(key.local.sin_port) ^ std::hash<uint16_t>()(key.remote.sin_port);
    }
};

// for those TCB owned by a socket.
static std::unordered_map<SocketPair, TCB*, SocketPairHash> active_tcb_map{};
static std::unordered_map<SocketPair, TCB*, SocketPairHash> orphaned_tcb_map{};
static std::unordered_map<uint16_t, SocketBlock*> listening_socket;

static int _tcp_close(TCB *tcb);

class PnxTcpInitailizer {
public:
    static void initialize() {
        tcb_recycler = std::thread{[&]() {
            while (tcb_recycler_can_stop.load() == false || orphaned_tcb.size() > 0) {
                auto task = orphaned_tcb.pop();
                if (!task.has_value()) continue;
                auto tcb = task.value();
                if (tcb->state == TCP_CLOSE) {
                    // stop the timer. （if it's not stopped yet)
                    tcb->timer_stop.store(true);
                    tcb->timer.join();


                    // remove from orphaned_tcb_map
                    std::unique_lock<std::mutex> lock(tcp_lock);
                    orphaned_tcb_map.erase({tcb->local, tcb->remote});
                    lock.unlock();

                    logInfo("tcb_recycler: delete tcb %x", tcb);
                    delete tcb;
                } else {
                    // put it back. simply busy waiting.
                    orphaned_tcb.push(tcb);
                }
            }
        }};
        
        add_exit_clean_up([&]() {
            std::unique_lock<std::mutex> lock(tcp_lock);

            // close all active connections.
            for (auto& pair : active_tcb_map) {
                TCB* tcb = pair.second;
                logInfo("tcb_recycler: close tcb %x", tcb);
                _tcp_close(tcb);
            }
            lock.unlock();
            // orphaned_tcbs close has been sent.

            tcb_recycler_can_stop.store(true);
            tcb_recycler.join();

        }, EXIT_CLEAN_UP_PRIORITY_TCP_RECVING);
    }
};

// every TCB has a timer thread for simplicity.
// check last segment timeout and launch retransmission.
static int _tcp_timer(TCB *tcb) {
    if (tcb->state == TCP_CLOSE) {
        // we do not need to do anything when the TCB is closed.
        return 0;
    }

    if (tcb->state == TCP_TIME_WAIT) {
        // wait 2MSL and close the connection.
        if (get_time_us() - tcb->send.last_sent_time >= 2 * kTcpMSL) {
            logDebug("state trans: TCP_TIME_WAIT -> TCP_CLOSE");
            tcb->state = TCP_CLOSE;
        }
        return 0;
    }

    // check if the last segment is timeout.
    if (tcb->send.waiting_for_ack()) {
        if (get_time_us() - tcb->send.last_sent_time >= kTcpTimeout) {

            if (tcb->send.retrans_count >= kTcpMaxRetrans) {
                // close the connection.
                logDebug("state trans: %d -> TCP_CLOSE", tcb->state);
                tcb->state = TCP_CLOSE;
                return 0;
            }

            logWarning("tcp_timer: retransmission timeout, retransmit the last segment");

            // update the last segment info.
            tcb->send.last_seg.hdr->ack_seq = htonl(tcb->recv.next);
            // tcb->send.last_seg.hdr->window = htons((uint16_t)tcb->recv.buf.rest_capacity());
            tcb->send.last_seg.fill_in_tcp_checksum();

            // retransmit the last segment.
            tcb->send.last_sent_time = get_time_us();
            tcb->send.retrans_count++;
            if (ip_send_packet(tcb->send.last_seg.src, tcb->send.last_seg.dst, IPPROTO_TCP, tcb->send.last_seg.buf, tcb->send.last_seg.len) != 0) {
                logWarning("tcp_timer: fail to retransmit the last segment");
                return -1;
            }
        }
    }
    return 0;
}

static int _init_TCB(TCB* tcb, const sockaddr_in *local, const sockaddr_in *remote, std::shared_ptr<Segment> syn) {
    // treat this as the start point of the TCP module.
    static bool init_done = false;
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);

    if (init_done == false) {
        init_done = true;
        PnxTcpInitailizer::initialize();
    }


    if (syn == nullptr) {
        // active open
        tcb->state = TCP_SYN_SENT;
        tcb->passive = false;
        tcb->local = *local;
        tcb->remote = *remote;

        { // init the sender part.
            tcb->send.init_seq = rand() % 10000;
            // tcb->send.remote_recv_window = 0;
            tcb->send.next = tcb->send.init_seq;
            tcb->send.unack = tcb->send.init_seq;
            tcb->send.retrans_count = 0;
            tcb->send.last_sent_time = 0;
        }

        { // init the receiver part.
            // we dont know the receiver part for an active open.
            tcb->recv.init_seq = 0;
            tcb->recv.next = 0;
            // tcb->recv.window = kTcpRecvBufferSize;
        }
    } else {
        assert(syn->hdr->syn == 1);

        tcb->state = TCP_SYN_RECV;
        tcb->passive = true;
        tcb->local = *local;
        tcb->remote = *remote;

        { // init the sender part.
            tcb->send.init_seq = rand() % 10000;
            // tcb->send.remote_recv_window = syn->hdr->window;
            tcb->send.next = tcb->send.init_seq;
            tcb->send.unack = tcb->send.init_seq;
            tcb->send.retrans_count = 0;
            tcb->send.last_sent_time = 0;
        }

        { // init the receiver part.
            tcb->recv.init_seq = syn->hdr->seq;
            tcb->recv.next = syn->hdr->seq + 1; // init_recv_seq used by SYN
            // tcb->recv.window = kTcpRecvBufferSize;
        }
    }

    tcb->timer = std::thread{[tcb]() {
        while (tcb->timer_stop.load() == false) {
            // sleep 10 ms
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
            
            std::lock_guard<std::mutex> lock(tcp_lock);
            if (tcb->state == TCP_CLOSE) {
                break;
            }

            if (_tcp_timer(tcb) != 0) {
                logWarning("tcp_timer: error happens");
            }
        }
    }};

    // make sure it's joinable
    assert(tcb->timer.joinable());

    return 0;
}

static int _tcp_send_pure_ACK(TCB *tcb) {
    Segment ack{sizeof(struct tcphdr)};
    ack.hdr->source = tcb->local.sin_port;
    ack.hdr->dest = tcb->remote.sin_port;
    ack.hdr->seq = tcb->send.next;
    ack.hdr->ack_seq = tcb->recv.next;
    ack.hdr->ack = 1;
    ack.hdr->doff = sizeof(struct tcphdr) / 4;
    ack.hdr->window = tcb->recv.buf.rest_capacity();

    ack.ntoh(); // reverse some fields

    ack.src = tcb->local.sin_addr;
    ack.dst = tcb->remote.sin_addr;

    ack.fill_in_tcp_checksum();

    logTrace("a pure ACK is sent");

    if (ip_send_packet(ack.src, ack.dst, IPPROTO_TCP, ack.buf, ack.len) != 0) { 
        logWarning("fail to send a pure ACK");
        return -1;
    }

    return 0;
}

static int _tcp_send_segment(TCB* tcb) {
    // construct a segment from tcb->send.buf.

    // when the buffer is empty, send a pure ACK, which does not require retransmission.
    // otherwise, send the first kTcpMaxSession bytes (at most), until a control seq.
    // if the first is a control signal, send it solely.

    if (tcb->send.waiting_for_ack() || tcb->send.buf.empty()) {
        // send a pure ACK.
        return _tcp_send_pure_ACK(tcb);
    }


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
        while (payload_len + sizeof(tcphdr) < kTcpMaxSegmentSize && !tcb->send.buf.empty() 
            // && (int64_t) payload_len < (int64_t) tcb->send.remote_recv_window - (tcb->send.next - tcb->send.unack)
            ) {
            Seq seq = tcb->send.buf.peek().value();
            if (seq.isCtrl()) break;
            segment[sizeof(tcphdr) + payload_len] = tcb->send.buf.pop()->byte;
            ++payload_len;
        }
    }
    
    Segment seg{segment, sizeof(struct tcphdr) + payload_len, tcb->local.sin_addr, tcb->remote.sin_addr};
    seg.ntoh();
    seg.fill_in_tcp_checksum();

    // tcb state update
    tcb->send.retrans_count = 0;
    tcb->send.last_sent_time = get_time_us();
    tcb->send.next += payload_len + seg.hdr->fin + seg.hdr->syn;
    tcb->send.last_seg = seg;

    logTrace("a segment is sent. payload_len=%llu, fin=%d, syn=%d", payload_len, seg.hdr->fin, seg.hdr->syn);

    if (ip_send_packet(seg.src, seg.dst, IPPROTO_TCP, seg.buf, seg.len) != 0) {
        logWarning("fail to send a segment");
        return -1;
    }

    return 0;
}

static int _tcp_make_sure_sendback(TCB *tcb) {
    // make sure there will be an event to send any update of tcb to the remote.
    if (tcb->send.waiting_for_ack() == false) {
        if (_tcp_send_segment(tcb) != 0) {
            logWarning("_tcp_makesure_sendback: fail to send segments");
            return -1;
        }
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

static TCB* _tcp_open(const sockaddr_in* local, const sockaddr_in* given_remote, std::shared_ptr<Segment> syn) {
    // if a SYN packet is given, then an active TCB is created.
    // otherwise a passive TCB is created, and jump to TCP_SYN_RECV state.
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
        logWarning("unimplemented tcp_open: already exists (active)");
        return nullptr;
    }

    if (orphaned_tcb_map.find(pair) != orphaned_tcb_map.end()) {
        logWarning("unimplemented tcp_open: already exists (orphaned)");
        return nullptr;
    }

    TCB* tcb = new TCB();

    logDebug("active_tcb_map[%s:%d, %s:%d] = %x", 
        inet_ntoa_safe(local->sin_addr).get(), ntohs(local->sin_port),
        inet_ntoa_safe(remote.sin_addr).get(), ntohs(remote.sin_port),
        tcb);
    active_tcb_map[pair] = tcb;

    _init_TCB(tcb, local, &remote, syn);

    if (syn != nullptr) {
        // passive open by a SYN, send back a SYN(ack)
        if (_tcp_send_ctrl(tcb, Seq{.syn = 1, .fin = 0, .byte = 0}) != 0) {
            logWarning("tcp_open: fail to send SYNACK");
            logDebug("state trans: _ -> TCP_CLOSE", tcb->state);
            tcb->state = TCP_CLOSE;
        }
    } else {
        // active open, send a SYN without ack.
        if (_tcp_send_ctrl(tcb, Seq{.syn = 1, .fin = 0, .byte = 0}) != 0) {
            logWarning("tcp_open: fail to send SYN");
            logDebug("state trans: _ -> TCP_CLOSE", tcb->state);
            tcb->state = TCP_CLOSE;
            return nullptr;
        }
    }

    return tcb;
}

TCB* tcp_open(const sockaddr_in* local, const sockaddr_in* given_remote, std::shared_ptr<Segment> syn) {
    std::lock_guard<std::mutex> lock(tcp_lock);
    return _tcp_open(local, given_remote, syn);
}

int tcp_register_listening_socket(SocketBlock *sb, uint16_t port /* network order */) {
    std::lock_guard<std::mutex> lock(tcp_lock);

    if (listening_socket.find(port) != listening_socket.end()) {
        logWarning("tcp_register_listening_socket: already exists");
        return -1;
    }

    listening_socket[port] = sb;
    return 0;
}

int tcp_unregister_listening_socket(SocketBlock *sb, uint16_t port) {
    std::lock_guard<std::mutex> lock(tcp_lock);

    if (listening_socket.find(port) == listening_socket.end()) {
        logWarning("tcp_unregister_listening_socket: not found");
        return -1;
    }
    if (listening_socket[port] != sb) {
        logWarning("tcp_unregister_listening_socket: not match");
        return -1;
    }
    listening_socket.erase(port);
    return 0;
}

static int _tcp_close(TCB *tcb) {
    // close the connection as soon as possible, and then recycle it.
    // send FIN and wait for CLOSED state.
    switch (tcb->state) {
        case TCP_CLOSE:
        case TCP_FIN_WAIT1:
        case TCP_FIN_WAIT2:
        case TCP_CLOSING:
        case TCP_LAST_ACK:
        case TCP_TIME_WAIT:
            break;

        case TCP_SYN_SENT:
        case TCP_LISTEN:

            logDebug("state trans: %d -> TCP_CLOSE", tcb->state);
            tcb->state = TCP_CLOSE;
            break;

        case TCP_SYN_RECV:
        case TCP_ESTABLISHED:
            logDebug("state trans: %d -> TCP_FIN_WAIT1", tcb->state);
            tcb->state = TCP_FIN_WAIT1;
            if (_tcp_send_ctrl(tcb, Seq{.syn = 0, .fin = 1, .byte = 0}) < 0) {
                logWarning("tcp_close: fail to send FIN");
                return -1;
            }
            break;
        case TCP_CLOSE_WAIT:
            logDebug("state trans: _ -> TCP_LAST_ACK", tcb->state);
            tcb->state = TCP_LAST_ACK;
            if (_tcp_send_ctrl(tcb, Seq{.syn = 0, .fin = 1, .byte = 0}) < 0) {
                logWarning("tcp_close: fail to send FIN");
                return -1;
            }
            break;
        default:
            return -1;
    }

    // insert into orphaned_tcb 
    orphaned_tcb.push(tcb);
    orphaned_tcb_map[SocketPair{tcb->local, tcb->remote}] = tcb; // we hold the lock. No race here.
    return 0;
}

int tcp_close(TCB *tcb) {
    std::unique_lock<std::mutex> lock(tcp_lock);

    // dont move this into _tcp_close since when we clean up, 
    // we iterate active_tcb_map and _tcp_close it.
    SocketPair pair{tcb->local, tcb->remote};
    active_tcb_map.erase(pair);

    return _tcp_close(tcb);
}


int tcp_send(TCB* tcb, const void *buf, int len) {
    // send is a non-blocking interface.

    std::unique_lock<std::mutex> lock(tcp_lock);
        
    // check state
    if (tcb->state != TCP_ESTABLISHED) {
        // for simplicity, we only support the simplest case.
        logWarning("tcp_send: not in ESTABLISHED state");
        return -1;
    }

    if (len < 0) {
        logWarning("tcp_send: negative length");
        return -1;
    }

    if (len == 0)
        return 0;

    // push the data into the buffer.
    int i = 0;
    for (i = 0; i < len; i++) {
        if (tcb->send.buf.push(Seq{.syn = 0, .fin = 0, .byte = ((char*)buf)[i]}) == 0) {
            break;
        }
    }

    if (_tcp_make_sure_sendback(tcb) < 0) {
        logWarning("tcp_send: fail to sendback");
        return -1;
    }
    return i;
}

int tcp_receive(TCB *tcb, void *buf, int len) {
    // recv is not blocking.

    std::unique_lock<std::mutex> lock(tcp_lock);

    // we only care about recv.buf, no matter what state we are.
    
    int recv = std::min(len, (int)tcb->recv.buf.size());
    assert(true == tcb->recv.buf.pop((char*) buf, recv));

    // int recv = 0;
    // while (recv < len && !tcb->recv.buf.empty()) {
    //     ((char*)buf)[recv++] = tcb->recv.buf.front();
    //     tcb->recv.buf.pop_front();
    // }

    return recv;
}

sockaddr_in tcp_getpeeraddress(TCB *tcb) { 
    std::lock_guard lock(tcp_lock);
    return tcb->remote;
}

int tcp_getstate(TCB *tcb) {
    std::lock_guard lock(tcp_lock);
    return tcb->state;
}

int tcp_no_data_incoming_state(int state) {
    return state == TCP_CLOSE || state == TCP_CLOSE_WAIT 
        || state == TCP_CLOSING || state == TCP_LAST_ACK || state == TCP_TIME_WAIT;
}

int tcp_can_send(int state) {
    return state == TCP_ESTABLISHED || state == TCP_CLOSE_WAIT;
}

static int _tcp_handle_segment_syn_recv(TCB *tcb, std::shared_ptr<Segment> seg) {
    // handle pure ack only
    if (seg->len != sizeof(struct tcphdr) || seg->hdr->syn == 1 || seg->hdr->fin == 1) {
        logWarning("tcp_handle_segment_syn_recv: not an pure ack");
        return -1;
    }

    // check if the ack is valid.
    if (seg->hdr->ack_seq != tcb->send.next) {
        logWarning("tcp_handle_segment_syn_recv: not acking my synack");
        return -1;
    }

    // check if the seq is valid.
    if (seg->hdr->seq != tcb->recv.init_seq + 1) {
        logWarning("tcp_handle_segment_syn_recv: strange seq");
        return -1;
    }

    // check if the window is valid.
    // if (seg->hdr->window > kTcpRecvBufferSize) {
    //     logWarning("tcp_handle_segment_syn_recv: too large window for me");
    //     return -1;
    // }

    if (seg->hdr->ack_seq > tcb->send.unack)
        tcb->send.unack = seg->hdr->ack_seq;

    logDebug("state trans: TCP_SYN_RECV -> TCP_ESTABLISHED");
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
    if (seg->hdr->ack_seq != tcb->send.init_seq + 1) {
        logWarning("tcp_handle_segment_syn_sent: not acking my syn");
        return -1;
    }

    // fill in remote info
    tcb->recv.init_seq = seg->hdr->seq;
    tcb->recv.next = seg->hdr->seq + 1; // init_recv_seq used by SYN
    // tcb->send.remote_recv_window = seg->hdr->window;

    if (seg->hdr->ack_seq > tcb->send.unack)
        tcb->send.unack = seg->hdr->ack_seq;

    logDebug("state trans: TCP_SYN_SENT -> TCP_ESTABLISHED");
    tcb->state = TCP_ESTABLISHED;
    if (_tcp_make_sure_sendback(tcb) < 0) {
        logWarning("tcp_handle_segment_syn_sent: fail to sendback");
        return -1;
    }
    return 0;
}

static int _tcp_handle_segment_established(TCB *tcb, std::shared_ptr<Segment> seg) {
    // normal or fin

    // handle ack update first
    if (seg->hdr->ack_seq > tcb->send.unack) {
        
        logDebug("tcp_handle_segment_established: ack upd. ack_seq=%u, unack=%u", seg->hdr->ack_seq, tcb->send.unack);
        tcb->send.unack = seg->hdr->ack_seq;
    }

    if (seg->have_payload())
        logTrace("tcp_handle_segment_established: recv %llu bytes", seg->payload_len());

    // push the data into the buffer.
    // TODO: we only recv whole segment, which is not efficient.
    // the reason we have to do so: we only accept segments whose first byte is exactly recv.next.

    if (tcb->recv.buf.push_all((char*)seg->buf.get() + sizeof(struct tcphdr), seg->payload_len()) == false) {
        // buffer overflow, drop this segment.
        logWarning("tcp_handle_segment_established: recv buffer overflow");
        return -1;
    }

    tcb->recv.next += seg->payload_len();

    // handle fin
    if (seg->hdr->fin == 1) {
        logDebug("state trans: TCP_ESTABLISHED -> TCP_CLOSE_WAIT");
        tcb->state = TCP_CLOSE_WAIT;
        tcb->recv.next++;
    }

    if (seg->need_to_ack())
        if (_tcp_make_sure_sendback(tcb) < 0) {
            logWarning("tcp_handle_segment_established: fail to sendback");
            return -1;
        }

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
        if (_tcp_make_sure_sendback(tcb) < 0) {
            logWarning("tcp_handle_segment_fin_wait1: fail to sendback");
            return -1;
        }
        tcb->state = TCP_CLOSING;
        
        // if my FIN is acked, trans to TCP_TIME_WAIT directly
        if (tcb->send.waiting_for_ack() == false) {
            tcb->state = TCP_TIME_WAIT;
        }

        return 0;
    }

    // case 2: without fin
    if (tcb->send.buf.empty() && tcb->send.waiting_for_ack() == false) {
        // my FIN is sent, and acked (by this segment).
        logDebug("state trans: TCP_FIN_WAIT1 -> TCP_FIN_WAIT2");
        tcb->state = TCP_FIN_WAIT2;
        return 0;
    }

    logWarning("unexpected case in tcp_handle_segment_fin_wait1");
    return -1;
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
    logDebug("state trans: TCP_FIN_WAIT2 -> TCP_TIME_WAIT");
    tcb->state = TCP_TIME_WAIT;
    if (_tcp_make_sure_sendback(tcb) < 0) {
        logWarning("tcp_handle_segment_fin_wait2: fail to sendback");
        return -1;
    }
    return 0;
}

static int _tcp_handle_segment_close_wait(TCB *tcb, std::shared_ptr<Segment> seg) {
    // abandon this seg since we have received a FIN.
    { (void)tcb; (void)seg; }
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

    logDebug("state trans: TCP_CLOSING -> TCP_TIME_WAIT");
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

    logDebug("state trans: TCP_LAST_ACK -> TCP_CLOSE");
    tcb->state = TCP_CLOSE;
    tcb->send.unack++;
    return 0;
}

static int _tcp_handle_segment_time_wait(TCB *tcb, std::shared_ptr<Segment> seg) {
    // if we receive a FIN again, ack back again, which is done by determining tcp_segment_handler();
    { (void)tcb; (void)seg; }
    return 0;
}

int tcp_segment_handler(const void* buf, int len, const struct in_addr& src, const struct in_addr& dst) {
    std::lock_guard<std::mutex> lock(tcp_lock);

    if (len < 0 || (size_t)len < sizeof(struct tcphdr)) {
        logWarning("tcp_segment_handler: too short to contain a tcp header");
        return -1;
    }

    // construct a Segment
    std::shared_ptr<Segment> seg = std::make_shared<Segment>(buf, len, src, dst);


    // the very first thing is to check the checksum.
    auto ck = seg->hdr->check;
    seg->fill_in_tcp_checksum();
    if (seg->hdr->check != ck) {
        logWarning("tcp_segment_handler: checksum error");
        return -1;
    }

    seg->ntoh();
    SocketPair tuple4;
    tuple4.local.sin_addr = dst;
    tuple4.local.sin_port = seg->hdr->dest;
    tuple4.remote.sin_addr = src;
    tuple4.remote.sin_port = seg->hdr->source;

    // handle SYN, i.e. connection openning.
    if (seg->hdr->syn == 1 && seg->hdr->ack == 0) {

        logInfo("tcp_segment_handler: recv a SYN");

        // check if there is a listening socket
        if (listening_socket.find(seg->hdr->dest) == listening_socket.end()) {
            logWarning("tcp_segment_handler: no listening socket");
            return -1;
        }
        SocketBlock *sb = listening_socket[seg->hdr->dest];
        
        TCB *tcb = _tcp_open(&tuple4.local, &tuple4.remote, seg);
        if (tcb == nullptr) {
            logWarning("tcp_segment_handler: reject to open a new connection");
            return -1;
        }
        assert(tcb->state == TCP_SYN_RECV); // wait for the ack of syn.

        socket_recv_new_tcp_conn(sb, tcb);

        return 0;
    }

    // for other cases, there should be a specific socket to handle this segment.
    TCB *tcb = nullptr;
    if (active_tcb_map.find(tuple4) != active_tcb_map.end()) {
        tcb = active_tcb_map[tuple4];
    } else if (orphaned_tcb_map.find(tuple4) != orphaned_tcb_map.end()) {
        tcb = orphaned_tcb_map[tuple4];
    } else {
        logWarning("tcp_segment_handler: no open socket can reponse. tuple4: from %s:%d to %s:%d", 
            inet_ntoa_safe(tuple4.local.sin_addr).get(), ntohs(tuple4.local.sin_port), 
            inet_ntoa_safe(tuple4.remote.sin_addr).get(), ntohs(tuple4.remote.sin_port));
        return -1;
    }

    if (tcb->state == TCP_CLOSE) {
        logWarning("tcp_segment_handler: recv a segment when the connection closed");
        return -1;
    }

    // the second thing is to check the seq.
    // if the seq does not match, we abandon this segment and clarify our progress again. 
    // reasons: maybe last connection with the same tuple4, or outdated segment, or ACK loss.
    
    if (tcb->state != TCP_SYN_SENT && tcb->recv.next != seg->hdr->seq) {
        // syn_sent state we dont have remote infomation.

        if (seg->hdr->ack_seq > tcb->send.unack) {
            tcb->send.unack = seg->hdr->ack_seq;
            // tcb->send.remote_recv_window = seg->hdr->window;
        }

        logWarning("tcp_handle_segment: seq not consistent %u != %u, ack back again.", tcb->recv.next, seg->hdr->seq);
        // sync our progress to remote.
        // possibily an ACK loss.
        if (_tcp_make_sure_sendback(tcb) < 0) {
            logWarning("tcp_handle_segment: fail to sendback");
            return -1;
        }

        return -1;
    }

    // Note:
    // in my partial implementation, any segment contains control bits will not contain data.

    // handle other segments except the first SYN
    assert(seg->hdr->ack == 1);

    switch (tcb->state) {
        case TCP_SYN_RECV:
            return _tcp_handle_segment_syn_recv(tcb, std::move(seg));
        case TCP_SYN_SENT:
            return _tcp_handle_segment_syn_sent(tcb, std::move(seg));
        case TCP_ESTABLISHED:
            return _tcp_handle_segment_established(tcb, std::move(seg));
        case TCP_FIN_WAIT1:
            return _tcp_handle_segment_fin_wait1(tcb, std::move(seg));
        case TCP_FIN_WAIT2:
            return _tcp_handle_segment_fin_wait2(tcb, std::move(seg));
        case TCP_CLOSE_WAIT:
            return _tcp_handle_segment_close_wait(tcb, std::move(seg));
        case TCP_CLOSING:
            return _tcp_handle_segment_closing(tcb, std::move(seg));
        case TCP_LAST_ACK:
            return _tcp_handle_segment_last_ack(tcb, std::move(seg));
        case TCP_TIME_WAIT:
            return _tcp_handle_segment_time_wait(tcb, std::move(seg));
        default:
            logWarning("tcp_segment_handler: invalid state");
            return -1;
    }
}


