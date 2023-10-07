#pragma once

#include <memory>
#include <netinet/tcp.h>
#include <cstring>
#include "pnx_utils.h"
#include "logger.h"


static uint16_t _tcp_checksum(const void* buf, int len, const struct in_addr& src, const struct in_addr& dst) {
    // calc the checksum of the ip header using the RFC specified algorithm.
    // https://tools.ietf.org/html/rfc1071

    // first set the checksum field to 0
    auto old_check = ((struct tcphdr*)buf)->check;

    ((struct tcphdr*)buf)->check = 0;

    /* Compute Internet Checksum for "count" bytes
    *         beginning at location "addr".
    */
    uint32_t sum = 0;

    int count = len;
    uint16_t * addr = (uint16_t *) buf;
    while( count > 1 )  {
        /*  This is the inner loop */
        sum += * (uint16_t*) addr++;
        count -= 2;
    }

    /*  Add left-over byte, if any */
    if ( count > 0 )
        sum += * (unsigned char *) addr;

    // add pseudo header
    sum += src.s_addr >> 16;
    sum += src.s_addr & 0xffff;
    sum += dst.s_addr >> 16;
    sum += dst.s_addr & 0xffff;
    sum += htons(IPPROTO_TCP);
    sum += htons(len);

    /*  Fold 32-bit sum to 16 bits */
    while (sum>>16)
        sum = (sum & 0xffff) + (sum >> 16);

    ((struct tcphdr*)buf)->check = old_check;
    return ~(uint16_t)sum;
}

struct Segment {
    std::shared_ptr<char[]> buf;
    size_t len;
    struct tcphdr * hdr;
    struct in_addr src;
    struct in_addr dst;

    // construct empty frame from a length.
    Segment(size_t len = 0) {
        this->buf = std::shared_ptr<char[]>(new char[len]);
        memset(this->buf.get(), 0, len);
        this->len = len;
        this->hdr = (struct tcphdr*)this->buf.get();
        this->src = this->dst = {};
    }

    // construct from a buffer
    Segment(const void *buf, size_t len, const struct in_addr& src, const struct in_addr &dst) : src(src), dst(dst) {
        // copy from buf to this->buf
        this->buf = std::shared_ptr<char[]>(new char[len]);
        memcpy(this->buf.get(), buf, len);
        this->len = len;
        this->hdr = (struct tcphdr*)this->buf.get();
    }


    void fill_in_tcp_checksum() {
        assert(this->len >= (int)sizeof(struct tcphdr));
        assert(this->src.s_addr != 0 && this->dst.s_addr != 0);
        logTrace("fill in tcp checksum. segment_len=%llu, src=%s, dst=%s", this->len, inet_ntoa_safe(this->src).get(), inet_ntoa_safe(this->dst).get());
        this->hdr->check = _tcp_checksum(this->buf.get(), this->len, this->src, this->dst);
    }

    bool have_payload() {
        return this->len > (int)sizeof(struct tcphdr);
    }

    inline void ntoh() {
        this->hdr->seq = ntohl(this->hdr->seq);
        this->hdr->ack_seq = ntohl(this->hdr->ack_seq);
        this->hdr->window = ntohs(this->hdr->window);
    }

    inline bool need_to_ack() {
        return have_payload() || this->hdr->fin || this->hdr->syn;
    }
};