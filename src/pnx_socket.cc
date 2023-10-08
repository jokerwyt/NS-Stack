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
#include "device.h"

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
        // use real socket to handle it.
        return __real_socket(domain, type, protocol);
    }

    // this function is the entry of the whole network stack for applications.
    // we add all devices here. 
    // TODO: handle some race.
    static bool pnx_initialized = false;
    if (pnx_initialized == false) {
        if (pnx_init_all_devices() != 0) {
            logError("device initialization failed.");
            return -1;
        }
        pnx_initialized = true;
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

    if (socket < kSocketMinFd) {
        // use real bind to handle it.
        return __real_bind(socket, address, address_len);
    }

    auto *sb = getSocketBlock(socket);
    if (sb == nullptr) {
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

    // memcpy(&sb->addr, address, sizeof(sockaddr_in));
    // We assume that the address is always not given by users.
    // But port must be given by the user.
    sockaddr_in *addr = (sockaddr_in*)address;

    if (dev_ip(0) == nullptr) {
        logWarning("device 0 is not initialized.");
        errno = EINVAL;
        return -1;
    }

    sb->addr.sin_family = AF_INET;
    sb->addr.sin_addr.s_addr = dev_ip(0)->s_addr; 
    sb->addr.sin_port = addr->sin_port;


    sb->state = SocketBlock::PASSIVE_BINDED;
    return 0;
}

int __wrap_listen(int socket, int backlog) {

    if (socket < kSocketMinFd) {
        // use real listen to handle it.
        return __real_listen(socket, backlog);
    }

    auto *sb = getSocketBlock(socket);
    if (sb == nullptr) {
        errno = EBADF;
        return -1;
    }

    if (backlog <= 0) {
        logWarning("invalid backlog.");
        errno = EINVAL;
        return -1;
    }

    if (sb->state != SocketBlock::PASSIVE_BINDED) {
        logWarning("cannot listen on a non-binded socket.");
        errno = EINVAL;
        return -1;
    }

    sb->state = SocketBlock::PASSIVE_LISTENING;
    sb->backlog = backlog;
    if (tcp_register_listening_socket(sb, sb->addr.sin_port) != 0) {
        logWarning("fail to register listening socket.");
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int __wrap_connect(int socket, const struct sockaddr *address, socklen_t address_len) {

    if (socket < kSocketMinFd) {
        // use real connect to handle it.
        return __real_connect(socket, address, address_len);
    }

    auto *sb = getSocketBlock(socket);
    if (sb == nullptr) {
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

    if (dev_ip(0) == nullptr) {
        logWarning("device 0 is not initialized.");
        errno = EINVAL;
        return -1;
    }

    // fill an client-side address.
    sb->addr.sin_family = AF_INET;
    sb->addr.sin_addr.s_addr = dev_ip(0)->s_addr;
    sb->addr.sin_port = rand() % 10000 + 10000; // ignore conflit for simplicity.

    sb->state = SocketBlock::ACTIVE;
    sb->tcb = tcp_open(&sb->addr, (const sockaddr_in*)address, nullptr);
    if (sb->tcb == nullptr) {
        logWarning("fail to open a TCP connection.");
        errno = EINVAL;
        return -1;
    }

    // wait until the connection is established.
    while (tcp_getstate(sb->tcb) != TCP_ESTABLISHED) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return 0;
}

int __wrap_accept(int socket, struct sockaddr *remote_addr, socklen_t *address_len) {

    if (socket < kSocketMinFd) {
        // use real accept to handle it.
        return __real_accept(socket, remote_addr, address_len);
    }

    auto *sb = getSocketBlock(socket);
    if (sb == nullptr) {
        errno = EBADF;
        return -1;
    }

    if (sb->state != SocketBlock::PASSIVE_LISTENING) {
        logWarning("only a passive-listening socket can accept new connection.");
        errno = EINVAL;
        return -1;
    }

    TCB* tcb = nullptr;
    while (tcb == nullptr) {
        {
            auto ac = sb->accepting.lock_mut();
            // find an ESTABLISHED connection.

            for (auto it = ac->begin(); it != ac->end(); it++) {
                if (tcp_getstate(*it) == TCP_ESTABLISHED) {
                    tcb = *it;
                    ac->erase(it);
                    break;
                }
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

    *address_len = sizeof(sockaddr_in);
    auto tmp = tcp_getpeeraddress(tcb);
    memcpy(remote_addr, &tmp, sizeof(sockaddr_in));

    return conn_sb->fd;
}

ssize_t __wrap_read(int fildes, void *buf, size_t nbyte) {

    if (fildes < kSocketMinFd) {
        // use real read to handle it.
        return __real_read(fildes, buf, nbyte);
    }

    auto *sb = getSocketBlock(fildes);
    if (sb == nullptr) {
        errno = EBADF;
        return -1;
    }

    if (sb->state != SocketBlock::ACTIVE) {
        logWarning("only an active socket can read");
        errno = EINVAL;
        return -1;
    }

    int ret = 0;
    while ((ret = tcp_receive(sb->tcb, buf, nbyte)) <= 0) {
        if (ret < 0) {
            return -1;
        }

        int state = tcp_getstate(sb->tcb);
        if (tcp_no_data_incoming_state(state)) {
            return 0;
        } else {
            // blocking.
            continue;
        }
    }
    return ret;
}

ssize_t __wrap_write(int fildes, const void *buf, size_t nbyte) {

    if (fildes < kSocketMinFd) {
        // use real read to handle it.
        return __real_write(fildes, buf, nbyte);
    }

    auto *sb = getSocketBlock(fildes);
    if (sb == nullptr) {
        errno = EBADF;
        return -1;
    }

    if (sb->state != SocketBlock::ACTIVE) {
        logWarning("only an active socket can read");
        errno = EINVAL;
        return -1;
    }

    int rest_len = nbyte;
    int done = 0;
    while (rest_len != 0) {
        int use = tcp_send(sb->tcb, (char*) buf + done, rest_len);
        if (use == 0) {
            int state = tcp_getstate(sb->tcb);
            if (tcp_can_send(state)) {
                // keep sending.
                continue;
            } else {
                return done;
            }
        } else if (use < 0) {
            return -1;
        } else {
            done += use;
            rest_len -= use;
        }
    }
    return nbyte;
}

int __wrap_close(int fildes) {

    if (fildes < kSocketMinFd) {
        // use real read to handle it.
        return __real_close(fildes);
    }

    auto *sb = getSocketBlock(fildes);
    if (sb == nullptr) {
        errno = EBADF;
        return -1;
    }

    if (sb->state == SocketBlock::ACTIVE) {
        if (tcp_close(sb->tcb) < 0) {
            logWarning("fail to close a TCP connection.");
            errno = EINVAL;
            return -1;
        }
    } else if (sb->state == SocketBlock::PASSIVE_BINDED || sb->state == SocketBlock::PASSIVE_LISTENING) {
        tcp_unregister_listening_socket(sb, sb->addr.sin_port);
        auto ac = sb->accepting.lock_mut();
        while (!ac->empty()) {
            if (tcp_close(ac->back()) < 0) {
                logWarning("fail to close a TCP connection.");
            }
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
    return __real_getaddrinfo(node, service, hints, res);

    // { (void)node; (void)service; (void)hints; (void)res; } // make compiler happy
    // logWarning("unimplemented getaddrinfo");
    // return 0;
}

int __wrap_setsockopt(int socket, int level, int option_name, const void *option_value, socklen_t option_len) {
    if (socket < kSocketMinFd) {
        // use real setsockopt to handle it.
        return __real_setsockopt(socket, level, option_name, option_value, option_len);
    }
    { (void)socket; (void)level; (void)option_name; (void)option_value; (void)option_len; } // make compiler happy
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
        if (ac->size() >= (size_t)sb->backlog) {
            logWarning("passive socket backlog is full.");
            errno = EINVAL;
            return -1;
        }
        ac->push_back(tcb);
    }

    logInfo("a new connection is added to socket %d", sb->fd);
    return 0;
}


struct sockaddr_in socket_get_localaddress(SocketBlock * sb) {
    return sb->addr;
}