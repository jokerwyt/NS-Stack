#include "pnx_socket.h"

#include <assert.h>
#include <atomic>
#include <memory>
#include <string.h>
#include <vector>
#include <netinet/tcp.h>
#include <unordered_map>
#include <thread>

#include "pnx_utils.h"
#include "logger.h"
#include "rustex.h"
#include "pnx_tcp.h"


struct SocketBlock {
    int fd;
    sockaddr_in addr;
    enum State {
        DEFAULT = 1,
        ACTIVE,
        PASSIVE_BINDED,
        PASSIVE_LISTENING,
        CLOSED
    } state;

    // only for active socket
    TCB* tcb;

    // only for PASSIVE_LISTENING socket.
    rustex::mutex<std::vector<TCB*>> accepting;
    // max backlog to-accept TCB
    int backlog;
};

const int kSocketMinFd = 1000;
std::atomic<int> next_fd{kSocketMinFd};


rustex::mutex<std::unordered_map<int, SocketBlock*>> sockets;

int __wrap_socket(int domain, int type, int protocol) {
    if (domain != AF_INET || type != SOCK_STREAM || protocol != 0) {
        errno = EAFNOSUPPORT;
        return -1;
    }

    auto block = new SocketBlock();
    block->fd = next_fd.fetch_add(1);
    memset(&block->addr, 0, sizeof(block->addr));
    block->state = SocketBlock::DEFAULT;
    block->tcb = nullptr;

    sockets.lock_mut()->insert({block->fd, block});
    return block->fd;
}

static SocketBlock* getSocketBlock(int socket) {
    auto ss = sockets.lock();
    auto it = ss->find(socket);
    if (it == ss->end()) {
        return nullptr;
    }

    return it->second;
}

int __wrap_bind(int socket, const struct sockaddr *address, socklen_t address_len) {
    auto *sb = getSocketBlock(socket);
    if (sb == nullptr) {
        // TODO: handle system fd.
        errno = EBADF;
        return -1;
    }

    if (sb->state != SocketBlock::DEFAULT) {
        logWarning("cannot bind a non-default socket.");
        errno = EINVAL;
        return -1;
    }

    if (address_len != sizeof(sockaddr_in)) {
        logWarning("invalid address length.");
        errno = EINVAL;
        return -1;
    }

    memcpy(&sb->addr, address, sizeof(sockaddr_in));
    sb->state = SocketBlock::PASSIVE_BINDED;
    return 0;
}

int __wrap_listen(int socket, int backlog) {
    auto *sb = getSocketBlock(socket);
    if (sb == nullptr) {
        // TODO: handle system fd.
        errno = EBADF;
        return -1;
    }

    if (sb->state != SocketBlock::PASSIVE_BINDED) {
        logWarning("cannot listen on a non-binded socket.");
        errno = EINVAL;
        return -1;
    }

    sb->state = SocketBlock::PASSIVE_LISTENING;
    sb->backlog = backlog;
    if (tcp_register_listening_socket(sb) != 0) {
        logWarning("fail to register listening socket.");
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int __wrap_connect(int socket, const struct sockaddr *address, socklen_t address_len) {
    auto *sb = getSocketBlock(socket);
    if (sb == nullptr) {
        // TODO: handle system fd.
        errno = EBADF;
        return -1;
    }

    if (sb->state != SocketBlock::DEFAULT) {
        logWarning("only a default socket can connect to other socket.");
        errno = EINVAL;
        return -1;
    }

    if (address_len != sizeof(sockaddr_in)) {
        logWarning("invalid address length.");
        errno = EINVAL;
        return -1;
    }

    sb->state = SocketBlock::ACTIVE;
    sb->tcb = tcp_active_open(&sb->addr, (const sockaddr_in*)address);
    if (sb->tcb == nullptr) {
        logWarning("fail to open a TCP connection.");
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int __wrap_accept(int socket, struct sockaddr *address, socklen_t *address_len) {
    auto *sb = getSocketBlock(socket);
    if (sb == nullptr) {
        // TODO: handle system fd.
        errno = EBADF;
        return -1;
    }

    if (sb->state != SocketBlock::PASSIVE_LISTENING) {
        logWarning("only a passive-listening socket can accept new connection.");
        errno = EINVAL;
        return -1;
    }

    if (sizeof(socklen_t) != sizeof(sockaddr_in)) {
        logWarning("invalid address length.");
        errno = EINVAL;
        return -1;
    }

    TCB* tcb = nullptr;
    while (tcb == nullptr) {
        {
            auto ac = sb->accepting.lock_mut();
            if (ac->empty() == false) {
                tcb = ac->back();
                ac->pop_back();
            }
        }
        if (tcb == nullptr)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    SocketBlock *conn_sb = new SocketBlock();
    conn_sb->fd = next_fd.fetch_add(1);
    conn_sb->addr = sb->addr;
    conn_sb->state = SocketBlock::ACTIVE;
    conn_sb->tcb = tcb;

    sockets.lock_mut()->insert({conn_sb->fd, conn_sb});

    return conn_sb->fd;
}

ssize_t __wrap_read(int fildes, void *buf, size_t nbyte) {
    auto *sb = getSocketBlock(fildes);
    if (sb == nullptr) {
        // TODO: handle system fd.
        errno = EBADF;
        return -1;
    }

    if (sb->state != SocketBlock::ACTIVE) {
        logWarning("only an active socket can read");
        errno = EINVAL;
        return -1;
    }

    return tcp_receive(sb->tcb, buf, nbyte);
}

ssize_t __wrap_write(int fildes, const void *buf, size_t nbyte) {
    auto *sb = getSocketBlock(fildes);
    if (sb == nullptr) {
        // TODO: handle system fd.
        errno = EBADF;
        return -1;
    }

    if (sb->state != SocketBlock::ACTIVE) {
        logWarning("only an active socket can read");
        errno = EINVAL;
        return -1;
    }

    return tcp_send(sb->tcb, buf, nbyte);
}

int __wrap_close(int fildes) {
    auto *sb = getSocketBlock(fildes);
    if (sb == nullptr) {
        // TODO: handle system fd.
        errno = EBADF;
        return -1;
    }

    if (sb->state == SocketBlock::ACTIVE) {
        tcp_close(sb->tcb);
    } else if (sb->state == SocketBlock::PASSIVE_BINDED || sb->state == SocketBlock::PASSIVE_LISTENING) {
        tcp_unregister_listening_socket(sb);
        auto ac = sb->accepting.lock_mut();
        while (!ac->empty()) {
            tcp_close(ac->back());
            ac->pop_back();
        }
    } else {
        logWarning("close a socket with invalid state");
        errno = EINVAL;
        return -1;
    }

    sb->state = SocketBlock::CLOSED;
    return 0;
}

int __wrap_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) {
    logWarning("unimplemented getaddrinfo");
    return 0;
}

int __wrap_setsockopt(int socket, int level, int option_name, const void *option_value, socklen_t option_len) {
    logWarning("unimplemented setsockopt");
    return 0;
}

int socket_recv_new_tcp_conn(SocketBlock *sb, TCB* tcb) {
    if (sb->state != SocketBlock::PASSIVE_LISTENING) {
        logWarning("only a passive-listening socket can accept new connection.");
        errno = EINVAL;
        return -1;
    }

    {
        auto ac = sb->accepting.lock_mut();
        if (ac->size() >= sb->backlog) {
            logWarning("passive socket backlog is full.");
            errno = EINVAL;
            return -1;
        }
        ac->push_back(tcb);
    }

    return 0;
}