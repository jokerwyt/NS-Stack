#pragma once

#include <memory>
#include <netinet/tcp.h>
#include <cstring>

struct Segment {
    std::shared_ptr<char[]> buf;
    size_t len;
    struct tcphdr * hdr;
    struct sockaddr_in src;

    Segment() : buf(nullptr), len(0), hdr(nullptr) {}
    Segment(void *buf, size_t len, const struct sockaddr_in& src) : src(src) {
        // copy from buf to this->buf
        this->buf = std::shared_ptr<char[]>(new char[len]);
        memcpy(this->buf.get(), buf, len);
        this->len = len;
        this->hdr = (struct tcphdr*)this->buf.get();
    }

    // forbid copy
    Segment(const Segment&) = delete;
    Segment& operator=(const Segment&) = delete;

};