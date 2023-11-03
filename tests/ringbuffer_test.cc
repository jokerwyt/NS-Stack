#include "ringbuffer.h"

#include <cassert>

int main() {
    RingBuffer<int, 1023> rb;


    int putcnt = 0;
    int getcnt = 0;

    for (int _ = 0; _ <= 1000; _ ++) {
        int buf[999];
        for(int i = 0; i < 999; i++) {
            buf[i] = ++putcnt;
        }

        assert(1 == rb.push_all(buf, 999));

        int buf2[999];
        assert(1 == rb.pop(buf2, 999));

        for(int i = 0; i < 999; i++) {
            assert(buf2[i] == ++getcnt);
        }
    }
}

