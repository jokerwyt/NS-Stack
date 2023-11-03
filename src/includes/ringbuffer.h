#pragma once

#include <cstring>
#include <optional>
#include <cassert>
#include <mutex>
#include <condition_variable>

// the array size is Capacity + 1, so that we can distinguish the empty case and full case.
template<typename T = char, int Capacity = 65535> 
class RingBuffer {
    static const size_t kArraySize = Capacity + 1;
    T buf[kArraySize];
    size_t next_pop, next_push;

public:
    RingBuffer() : next_pop(0), next_push(0) {
        memset(buf, 0, sizeof buf);
    }

    bool empty() { return next_pop == next_push; }
    bool full() { return (next_push + 1) % kArraySize == next_pop; }
    size_t size() { return (next_push - next_pop + kArraySize) % kArraySize; }

    bool push(T a) {
        if (full()) return 0;
        buf[next_push++] = a;
        next_push %= kArraySize;
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
        next_pop %= kArraySize;
        return buf[rem];
    }

    size_t rest_capacity() {
        return Capacity - size();
    }

private:
    size_t one_push_capacity() {
        // capacity until reaching the following cases:
        // 1. full case, next_push == next_pop - 1
        // 2. reach the end of array, next_push == 0

        if (next_push < next_pop) {
            return next_pop - 1 - next_push;
        } else {
            // see which case will be reached first
            if (next_pop == 0) {
                // next_push can only reach the end of array, i.e. kArraySize - 1
                return kArraySize - 1 - next_push;
            } else {
                // next_push can reach 0
                return kArraySize - next_push;
            }
        }
    }

    size_t one_pop_capacity() {
        // capacity until reaching the following cases:
        // 1. empty case, next_push == next_pop
        // 2. reach the end of array, next_pop == 0

        if (next_push > next_pop) {
            return next_push - next_pop;
        } else {
            // case 2 will reach first, or both cases will reach at the same time
            return kArraySize - next_pop;
        }
    }
    
public:
    size_t try_push(T *a, size_t len) {
        size_t rem = std::min(len, this->one_push_capacity());
        memcpy(buf + next_push, a, rem * sizeof(T));
        next_push += rem;
        if (next_push == kArraySize) {
            next_push = 0;
            assert(next_pop != 0);
        }
        return rem;
    }

    bool push_all(T *a, size_t len) {
        if (rest_capacity() < len) return 0;

        size_t rest = len;
        int cnt = 0;
        while (rest > 0) {
            size_t pushed = try_push(a, rest);
            assert(pushed > 0);
            rest -= pushed;
            a += pushed;

            if (++cnt > 2) {
                assert(0);
            }
        }
        return 1;
    }

    size_t try_pop(T *a, size_t max_len) {
        size_t rem = std::min(max_len, this->one_pop_capacity());
        memcpy(a, buf + next_pop, rem * sizeof(T));
        next_pop += rem;
        if (next_pop == kArraySize) {
            next_pop = 0;
        }
        return rem;
    }

    bool pop(T *a, size_t len) {
        if (size() < len) return 0;

        size_t rest = len;
        int cnt = 0;
        while (rest > 0) {
            size_t popped = try_pop(a, rest);
            assert(popped > 0);
            rest -= popped;
            a += popped;

            if (++cnt > 2) {
                assert(0);
            }
        }
        return 1;
    }
};


template<typename T = char, int Capacity = 65535> 
class BlockingRingBuffer : private RingBuffer<T, Capacity> {
    std::mutex mutex;
    std::condition_variable cv_for_pop, cv_for_push;

public:
    bool push(T a) {
        std::unique_lock<std::mutex> lock(mutex);
        
        int cnt = 0;
        while (this->full()) {
            cv_for_push.wait_for(lock, std::chrono::milliseconds(100));
            if (++cnt > 2) {
                return false;
            }
        }
        RingBuffer<T, Capacity>::push(a);
        cv_for_pop.notify_one();
        return true;
    }

    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex);
        int cnt = 0;
        while (this->empty()) {
            cv_for_pop.wait_for(lock, std::chrono::milliseconds(100));
            if (++cnt > 2) {
                return std::nullopt;
            }
        }
        T ret = RingBuffer<T, Capacity>::pop().value();
        cv_for_push.notify_one();
        return ret;
    }

    size_t size() {
        std::unique_lock<std::mutex> lock(mutex);
        return RingBuffer<T, Capacity>::size();
    }
};