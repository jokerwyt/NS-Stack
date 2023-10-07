#pragma once


// real capacity is Capacity - 1
template<typename T = char, int Capacity = 65536> 
class RingBuffer {
    size_t cap = Capacity + 1;
    T buf[Capacity + 1];
    size_t next_pop, next_push;
    // size_t next_pop_seq; // a convenient set for TCP.

public:
    RingBuffer() : next_pop(0), next_push(0) {
        memset(buf, 0, sizeof buf);
    }

    bool empty() { return next_pop == next_push; }
    bool full() { return (next_push + 1) % cap == next_pop; }
    int size() { return (next_push - next_pop + cap) % cap; }
    

    bool push(T a) {
        if (full()) return 0;
        buf[next_push++] = a;
        next_push %= cap;
        return 1;
    }

    std::optional<T> peek() {
        if (empty())
            return std::nullopt;
        return buf[next_pop];
    }

    std::optional<T> pop() {
        if (empty())
            return std::nullopt;
        auto rem = next_pop;
        ++next_pop;
        next_pop %= cap;
        return buf[rem];
    }

    size_t rest_capacity() {
        return cap - size() - 1;
    }
    
    // void set_next_pop_seq(size_t x) { next_pop_seq = x; }
    // size_t get_next_pop_seq(size_t) { return this->next_pop_seq; }

    // TODO: other funtionalities.
};