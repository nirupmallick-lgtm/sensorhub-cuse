#include "sensorhub/hub_core.hpp"

#include <cerrno>
#include <chrono>
#include <sstream>
#include <string>

namespace sensorhub {

namespace {
uint64_t now_ns() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}
}  // namespace

HubCore::HubCore(size_t capacity, std::vector<std::unique_ptr<ISensor>> sensors)
    : ring_(capacity), sensors_(std::move(sensors)) {
    cfg_.sensor_mask = kAllSensorsMask;
    cfg_.period_ms = kDefaultPeriodMs;
}

HubCore::~HubCore() { stop(); }

void HubCore::set_listener(Listener l) {
    std::lock_guard<std::mutex> lk(m_);
    listener_ = std::move(l);
}

int HubCore::set_config(const Config& c) {
    if (c.period_ms < kMinPeriodMs || c.period_ms > kMaxPeriodMs) return EINVAL;
    if (c.sensor_mask == 0 || (c.sensor_mask & ~kAllSensorsMask) != 0) return EINVAL;
    {
        std::lock_guard<std::mutex> lk(m_);
        cfg_.sensor_mask = c.sensor_mask;
        cfg_.period_ms = c.period_ms;
        cfg_dirty_ = true;
    }
    cv_.notify_all();
    return 0;
}

Config HubCore::get_config() const {
    std::lock_guard<std::mutex> lk(m_);
    return cfg_;
}

bool HubCore::running() const {
    std::lock_guard<std::mutex> lk(m_);
    return cfg_.running != 0;
}

void HubCore::start() {
    std::lock_guard<std::mutex> life(lifecycle_m_);
    if (thread_.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(m_);
        stop_req_ = false;
        cfg_.running = 1;
    }
    thread_ = std::thread(&HubCore::loop, this);
}

void HubCore::stop() {
    std::lock_guard<std::mutex> life(lifecycle_m_);
    if (!thread_.joinable()) return;
    {
        std::lock_guard<std::mutex> lk(m_);
        stop_req_ = true;
    }
    cv_.notify_all();
    thread_.join();
    std::lock_guard<std::mutex> lk(m_);
    cfg_.running = 0;
}

void HubCore::loop() {
    std::unique_lock<std::mutex> lk(m_);
    while (!stop_req_) {
        auto period = std::chrono::milliseconds(cfg_.period_ms);
        cv_.wait_for(lk, period, [&] { return stop_req_ || cfg_dirty_; });
        if (stop_req_) break;
        if (cfg_dirty_) { cfg_dirty_ = false; continue; }  // restart wait with new period
        lk.unlock();
        poll_once();
        lk.lock();
    }
}

size_t HubCore::poll_once() {
    std::lock_guard<std::mutex> pl(poll_m_);
    uint32_t mask;
    Listener listener;
    {
        std::lock_guard<std::mutex> lk(m_);
        mask = cfg_.sensor_mask;
        listener = listener_;
    }
    size_t n = 0;
    for (auto& s : sensors_) {
        if (!(mask & (1u << s->id()))) continue;
        auto v = s->read();
        if (!v) continue;  // source unavailable: skip, never fabricate data
        Sample smp{};
        smp.timestamp_ns = now_ns();
        smp.sensor_id = s->id();
        smp.sequence = seq_.fetch_add(1);
        smp.value_milli = *v;
        ring_.push(smp);
        ++n;
    }
    produced_ += n;
    if (listener) listener(n);
    return n;
}

size_t HubCore::read(Sample* out, size_t max) {
    size_t n = ring_.pop(out, max);
    consumed_ += n;
    return n;
}

size_t HubCore::available() const { return ring_.size(); }
void HubCore::flush() { ring_.clear(); }

Stats HubCore::stats() const {
    Stats s{};
    s.produced = produced_.load();
    s.consumed = consumed_.load();
    s.dropped = ring_.dropped();
    s.fill = static_cast<uint32_t>(ring_.size());
    s.capacity = static_cast<uint32_t>(ring_.capacity());
    return s;
}

int apply_command(HubCore& core, std::string_view line) {
    std::istringstream is{std::string(line)};
    std::string cmd;
    if (!(is >> cmd)) return EINVAL;

    if (cmd == "start") { core.start(); return 0; }
    if (cmd == "stop")  { core.stop();  return 0; }
    if (cmd == "flush") { core.flush(); return 0; }
    if (cmd == "period" || cmd == "mask") {
        std::string arg;
        if (!(is >> arg)) return EINVAL;
        unsigned long val = 0;
        try {
            size_t pos = 0;
            val = std::stoul(arg, &pos, 0);  // base auto-detect: 10 / 0x
            if (pos != arg.size() || val > 0xFFFFFFFFul) return EINVAL;
        } catch (...) { return EINVAL; }
        Config c = core.get_config();
        if (cmd == "period") c.period_ms = static_cast<uint32_t>(val);
        else                 c.sensor_mask = static_cast<uint32_t>(val);
        return core.set_config(c);
    }
    return EINVAL;
}

}  // namespace sensorhub
