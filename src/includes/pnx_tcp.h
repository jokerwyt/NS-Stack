#pragma once


/* 
    Design doc of TCP layer.

    This file implements a ToyCP protocol, which subsets and modifies the TCP protocol slightly.
    Most modification is for simplicity. We basically follow RFC793 when making design decisions.

Functionalities.
    Support reliable transmission.
    Including TCP connections manegement, state machine, timer, internal buffer, etc.

Users.
    Three threads will access this module: 
        1. timer thread, (module internal)
        2. listener thread, i.e. tcp_segment_handler, (lower IP layer)
        3. user thread. (upper socket layer)

Synchronizations.
    The module provides internal synchronizations, i.e. all functions are thread-safe.

Resource management.
    For simplicity, we suppose the system have enough resource and do not need to recycle.

*/

#include <netinet/tcp.h>

struct TCB;
struct Segment;

// interface for socket layer.
TCB* tcp_open(const struct sockaddr_in *local, const struct sockaddr_in *remote, std::shared_ptr<Segment> syn);
bool tcp_register_listening_socket(SocketBlock *sb, uint16_t port /* network order */);
bool tcp_unregister_listening_socket(SocketBlock *sb, uint16_t port /* network order */);
int tcp_close(TCB* tcb);
int tcp_send(TCB* tcb, const void *buf, int len);
int tcp_receive(TCB* tcb, void *buf, int len);
struct sockaddr_in tcp_getpeeraddress(TCB* tcb);


// interface for ip layer.
int tcp_segment_handler(const void* buf, int len, const struct in_addr& src, const struct in_addr& dst);
