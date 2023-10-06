#pragma once


/* 
    Design doc.

Functionalities.
    Support TCP connections manegement, including state machine, timer, internal buffer.

    Three threads will access this module: 
        1. timer thread, (module internal)
        2. listener thread, i.e. tcp_segment_handler, (lower layer)
        3. user thread. (upper layer)


Synchronizations.
    The whole module is protected by a single lock for simplicity.

*/


#include <netinet/tcp.h>
#include <deque>


struct TCB;

// interface for socket layer.
TCB* tcp_active_open(const struct sockaddr_in *local, const struct sockaddr_in *remote);
bool tcp_register_listening_socket(SocketBlock *sb);
bool tcp_unregister_listening_socket(SocketBlock *sb);
void tcp_close(TCB* tcb);
int tcp_send(TCB* tcb, const void *buf, int len);
int tcp_receive(TCB* tcb, void *buf, int len);
struct sockaddr_in tcp_getpeeraddress(TCB* tcb);


// interface for ip layer.
int tcp_segment_handler(const void* buf, int len, const struct sockaddr_in& src);
