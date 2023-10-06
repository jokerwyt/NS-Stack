#pragma once



/* 
    Design Doc of Socket layer.

    We suppose our user is single thread for simplicity.

    Socket layer will heavily interact with the tcp layer. 
    1. a socket may actively send operation to a tcp connection.
    2. a tcp connection may be sent to a socket for accept()
*/



#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

/**
* @see [POSIX.1-2017:socket](http://pubs.opengroup.org/onlinepubs/ 
* 9699919799/functions/socket.html)
*/
int __wrap_socket(int domain, int type, int protocol);

/**
* @see [POSIX.1-2017:bind](http://pubs.opengroup.org/onlinepubs/ 
* 9699919799/functions/bind.html)
*/
int __wrap_bind(int socket, const struct sockaddr *address, socklen_t address_len);

/**
* @see [POSIX.1-2017:listen](http://pubs.opengroup.org/onlinepubs/ 
* 9699919799/functions/listen.html)
*/
int __wrap_listen(int socket, int backlog);

/**
* @see [POSIX.1-2017:connect](http://pubs.opengroup.org/onlinepubs/ 
* 9699919799/functions/connect.html)
*/
int __wrap_connect(int socket, const struct sockaddr *address, socklen_t address_len);

/**
* @see [POSIX.1-2017:accept](http://pubs.opengroup.org/onlinepubs/ 
* 9699919799/functions/accept.html)
*/
int __wrap_accept(int socket, struct sockaddr *address, socklen_t *address_len);

/**
* @see [POSIX.1-2017:read](http://pubs.opengroup.org/onlinepubs/ 
* 9699919799/functions/read.html)
*/
ssize_t __wrap_read(int fildes, void *buf, size_t nbyte);

/**
* @see [POSIX.1-2017:write](http://pubs.opengroup.org/onlinepubs/ 
* 9699919799/functions/write.html)
*/
ssize_t __wrap_write(int fildes, const void *buf, size_t nbyte);

/**
* @see [POSIX.1-2017:close](http://pubs.opengroup.org/onlinepubs/ 
* 9699919799/functions/close.html)
*/
int __wrap_close(int fildes);

/**
* @see [POSIX.1-2017:getaddrinfo](http://pubs.opengroup.org/onlinepubs/
* 9699919799/functions/getaddrinfo.html) 
*/
int __wrap_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);

int __wrap_setsockopt(int socket, int level, int option_name, const void *option_value, socklen_t option_len);


// my custom interface
struct SocketBlock;
struct TCB;

int socket_recv_new_tcp_conn(SocketBlock *sb, TCB* tcb);
