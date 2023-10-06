#include "socket.h"

#include <assert.h>
#include <atomic>
#include <memory>
#include <string.h>
#include <vector>
#include <netinet/tcp.h>

#include "logger.h"
#include "pnx_utils.h"
#include "rustex.h"
#include "pnx_tcp.h"

static const int kSocketFdMin = 1000;
static const int kMaxSocketFd = 10000;
static std::atomic<int> socket_fd_counter(kSocketFdMin);

class SocketBlock {
private:
    int fd_;
    sockaddr_in addr_;
    
    enum State {
        OFF = 1,
        ACTIVE,
        PASSIVE,
        PASSIVE_BINDED,
        PASSIVE_LISTENING
    };
    State state = OFF;

    size_t tcb;
    // only for PASSIVE_LISTENING socket
    std::vector<size_t> prepared_conns; // conns for accept()

public:
    SocketBlock(int fd) : fd_(fd) {
        // check fd
        assert(fd_ >= kSocketFdMin && fd_ <= kMaxSocketFd);
        
        // clear addr_
        memset(&addr_, 0, sizeof(addr_));
    }

    int fd() const { return fd_; }

    sockaddr_in addr() const { return addr_; }
    // get port
    uint16_t port() const { return ntohs(addr_.sin_port); }
    // address setter
    void set_addr(const sockaddr_in& addr) { addr_ = addr; }
};

// we have a lock for each SocketBlock object.
std::vector<rustex::mutex<std::shared_ptr<SocketBlock>>> sockets;

static void _initialize() {
    socket_infos.resize(kMaxSocketFd);
    port_listener.resize(kMaxSocketFd);
}

int __wrap_socket(int domain, int type, int protocol) {
    static std::atomic<bool> init_start = false;
    static std::atomic<bool> init_done = false;
    if (init_start.exchange(true) == false) {
        _initialize();
        init_done.store(true);
    }
    while (init_done.load() == false) {
        // wait for initialization
    }




    // sanity check: just a partial implementation of RFC.
    assert(domain == AF_INET);
    assert(type == SOCK_STREAM);
    assert(protocol == 0);

    auto fd = socket_fd_counter.fetch_add(1);
    if (fd > kMaxSocketFd) {
        logError("socket fd exceeds max limit");
        return -1;
    }
    *socket_infos[fd].lock_mut() = std::make_shared<SocketBlock>(fd);
    return fd;
}

static std::optional<rustex::mutex<std::shared_ptr<SocketBlock>>&> _get_socket_info(int fd) {
    if (fd < kSocketFdMin || fd > kMaxSocketFd) {
        logError("invalid socket fd");
        return std::nullopt;
    }
    rustex::mutex<std::shared_ptr<SocketBlock>> &sinfo = socket_infos[fd];

    if (sinfo.lock()->get() == nullptr) {
        logError("socket not found");
        return std::nullopt;
    }

    return sinfo;
}

int __wrap_bind(int socket, const struct sockaddr *address, socklen_t address_len) {
    auto sinfo = _get_socket_info(socket);
    if (sinfo.has_value() == false) {
        logError("socket not found");
        return -1;
    }
    
    assert(address_len == sizeof(sockaddr_in)); // as a partial implementation
    (*sinfo.value().lock_mut())->set_addr(*reinterpret_cast<const sockaddr_in*>(address));
    return 0;
}

int __wrap_listen(int socketfd, int backlog) {
    auto sinfo = _get_socket_info(socketfd);
    if (sinfo.has_value() == false) {
        logError("socket not found");
        return -1;
    }

    // get the port
    auto port = (*sinfo.value().lock_mut())->port();
    if (port_listener[port].load().has_value()) {
        logError("port already listened");
        return -1;
    }

    // set the port
    port_listener[port].store(socketfd);
    return 0;
}

int __wrap_setsockopt(int socket, int level, int option_name,
                      const void* option_value, socklen_t option_len) {
    // warn and do nothing
    logWarning("setsockopt is not implemented");
    return 0;
}
