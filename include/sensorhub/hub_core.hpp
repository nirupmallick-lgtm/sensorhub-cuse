// hub_core.hpp - Driver-independent core: acquisition thread + ring buffer.
// Contains NO FUSE/CUSE code, so it is fully unit-testable.
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

#include "sensorhub/abi.hpp"
#include "sensorhub/ring_buffer.hpp"
#include "sensorhub/sensor.hpp"

namespace sensorhub {

class HubCore {
public:
    // Called (without any HubCore lock held) after every acquisition tick.
    using Listener = std::function<void(size_t produced_this_tick)>;

    HubCore(size_t capacity, std::vector<std::unique_ptr<ISensor>> sensors);
    ~HubCore();
    HubCore(const HubCore&) = delete;
    HubCore& operator=(const HubCore&) = delete;

    void set_listener(Listener l);

    // Returns 0 or a positive errno (EINVAL). Ignores cfg.running / cfg.reserved.
    int set_config(const Config& cfg);
    Config get_config() const;

    void start();   // idempotent
    void stop();    // idempotent, joins the thread
    bool running() const;

    size_t poll_once();                       // one synchronous tick (also used by tests)
    size_t read(Sample* out, size_t max);     // non-blocking, oldest first
    size_t available() const;
    void flush();
    Stats stats() const;

private:
    void loop();

    RingBuffer<Sample> ring_;
    std::vector<std::unique_ptr<ISensor>> sensors_;

    mutable std::mutex m_;            // protects cfg_, flags, listener_
    std::condition_variable cv_;
    Config cfg_{};
    bool stop_req_ = false;
    bool cfg_dirty_ = false;
    Listener listener_;

    std::mutex lifecycle_m_;          // serialises start()/stop()
    std::thread thread_;
    std::mutex poll_m_;               // sensors are not thread-safe

    std::atomic<uint64_t> produced_{0};
    std::atomic<uint64_t> consumed_{0};
    std::atomic<uint32_t> seq_{0};
};

// Text command interface used by write(2):  "start", "stop", "flush",
// "period <ms>", "mask <n>".  Returns 0 or a positive errno.
int apply_command(HubCore& core, std::string_view line);

}  // namespace sensorhub
