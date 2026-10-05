#include <thread>
#include <atomic>
#include "check.hpp"
#include "sensorhub/ring_buffer.hpp"
using sensorhub::RingBuffer;

static void test_fifo_order() {
    RingBuffer<int> rb(4);
    for (int i = 1; i <= 3; ++i) CHECK(!rb.push(i));
    int out[4];
    CHECK(rb.pop(out, 4) == 3);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3);
    CHECK(rb.size() == 0);
}
static void test_overwrite_oldest() {
    RingBuffer<int> rb(3);
    for (int i = 1; i <= 5; ++i) rb.push(i);  // 1,2 dropped
    CHECK(rb.dropped() == 2);
    CHECK(rb.size() == 3);
    int out[3];
    CHECK(rb.pop(out, 3) == 3);
    CHECK(out[0] == 3 && out[1] == 4 && out[2] == 5);
}
static void test_partial_pop_and_wrap() {
    RingBuffer<int> rb(4);
    for (int i = 0; i < 4; ++i) rb.push(i);
    int out[2];
    CHECK(rb.pop(out, 2) == 2);
    rb.push(10); rb.push(11);  // wraps around
    int all[8];
    CHECK(rb.pop(all, 8) == 4);
    CHECK(all[0] == 2 && all[1] == 3 && all[2] == 10 && all[3] == 11);
}
static void test_clear_and_invalid() {
    RingBuffer<int> rb(2);
    rb.push(1); rb.clear();
    CHECK(rb.size() == 0);
    bool threw = false;
    try { RingBuffer<int> bad(0); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}
static void test_concurrent_accounting() {
    RingBuffer<int> rb(64);
    std::atomic<bool> done{false};
    std::atomic<size_t> popped{0};
    std::thread consumer([&] {
        int buf[16];
        while (!done || rb.size() > 0) popped += rb.pop(buf, 16);
    });
    std::thread p1([&] { for (int i = 0; i < 20000; ++i) rb.push(i); });
    std::thread p2([&] { for (int i = 0; i < 20000; ++i) rb.push(i); });
    p1.join(); p2.join(); done = true; consumer.join();
    CHECK(popped + rb.dropped() == 40000);  // nothing lost silently
}

int main() {
    RUN(test_fifo_order);
    RUN(test_overwrite_oldest);
    RUN(test_partial_pop_and_wrap);
    RUN(test_clear_and_invalid);
    RUN(test_concurrent_accounting);
    TEST_MAIN_END();
}
