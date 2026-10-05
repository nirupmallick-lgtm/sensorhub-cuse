// ring_buffer.hpp - Thread-safe fixed-capacity circular buffer (overwrite-oldest).
// User-space analogue of the kernel's kfifo / circ_buf used in many drivers.
#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace sensorhub {

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity) : buf_(capacity) {
        if (capacity == 0) throw std::invalid_argument("RingBuffer capacity must be > 0");
    }

    // Pushes one item. If the buffer is full the OLDEST item is discarded
    // (a sensor stream prefers fresh data) and the drop counter is incremented.
    // Returns true when an old item was dropped.
    bool push(const T& v) {
        std::lock_guard<std::mutex> lk(m_);
        bool dropped = false;
        if (count_ == buf_.size()) {
            head_ = (head_ + 1) % buf_.size();
            --count_;
            ++dropped_;
            dropped = true;
        }
        buf_[(head_ + count_) % buf_.size()] = v;
        ++count_;
        return dropped;
    }

    // Pops up to `max` items (oldest first) into `out`. Returns number popped.
    size_t pop(T* out, size_t max) {
        std::lock_guard<std::mutex> lk(m_);
        size_t n = count_ < max ? count_ : max;
        for (size_t i = 0; i < n; ++i) {
            out[i] = buf_[head_];
            head_ = (head_ + 1) % buf_.size();
        }
        count_ -= n;
        return n;
    }

    size_t size() const { std::lock_guard<std::mutex> lk(m_); return count_; }
    size_t capacity() const { return buf_.size(); }
    uint64_t dropped() const { std::lock_guard<std::mutex> lk(m_); return dropped_; }
    void clear() { std::lock_guard<std::mutex> lk(m_); head_ = 0; count_ = 0; }

private:
    mutable std::mutex m_;
    std::vector<T> buf_;
    size_t head_ = 0;
    size_t count_ = 0;
    uint64_t dropped_ = 0;
};

}  // namespace sensorhub
