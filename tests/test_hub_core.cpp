#include <atomic>
#include <cerrno>
#include <chrono>
#include <thread>
#include "check.hpp"
#include "sensorhub/hub_core.hpp"
using namespace sensorhub;

namespace {
struct FakeSensor : ISensor {
    explicit FakeSensor(uint32_t i, int32_t v) : id_(i), v_(v) {}
    uint32_t id() const override { return id_; }
    const char* name() const override { return "FAKE"; }
    std::optional<int32_t> read() override { return v_; }
    uint32_t id_; int32_t v_;
};
std::vector<std::unique_ptr<ISensor>> sim_only() {
    std::vector<std::unique_ptr<ISensor>> v;
    v.push_back(std::make_unique<SimWaveSensor>());
    return v;
}
bool wait_for(const std::function<bool()>& pred, int ms = 3000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}
}  // namespace

static void test_poll_and_read() {
    HubCore hub(8, sim_only());
    CHECK(hub.poll_once() == 1);
    CHECK(hub.available() == 1);
    Sample s{};
    CHECK(hub.read(&s, 1) == 1);
    CHECK(s.sensor_id == kSensorSimWave && s.value_milli == 25000 && s.sequence == 0);
    CHECK(hub.read(&s, 1) == 0);  // empty -> non-blocking returns 0
    CHECK(hub.stats().produced == 1 && hub.stats().consumed == 1);
}
static void test_config_validation() {
    HubCore hub(8, sim_only());
    Config c = hub.get_config();
    c.period_ms = 1;              CHECK(hub.set_config(c) == EINVAL);
    c.period_ms = 100000;         CHECK(hub.set_config(c) == EINVAL);
    c.period_ms = 50; c.sensor_mask = 0;     CHECK(hub.set_config(c) == EINVAL);
    c.sensor_mask = 0x10;         CHECK(hub.set_config(c) == EINVAL);
    c.sensor_mask = 0x8;          CHECK(hub.set_config(c) == 0);
    CHECK(hub.get_config().period_ms == 50 && hub.get_config().sensor_mask == 0x8);
}
static void test_overflow_drops_oldest() {
    HubCore hub(4, sim_only());
    for (int i = 0; i < 6; ++i) hub.poll_once();
    Stats st = hub.stats();
    CHECK(st.dropped == 2 && st.fill == 4 && st.capacity == 4 && st.produced == 6);
    Sample out[8];
    CHECK(hub.read(out, 8) == 4);
    CHECK(out[0].sequence == 2 && out[3].sequence == 5);  // newest data kept, in order
}
static void test_mask_filters_sensors() {
    std::vector<std::unique_ptr<ISensor>> v;
    v.push_back(std::make_unique<FakeSensor>(kSensorLoadAvg, 1000));
    v.push_back(std::make_unique<SimWaveSensor>());
    HubCore hub(8, std::move(v));
    Config c = hub.get_config(); c.sensor_mask = 1u << kSensorSimWave;
    CHECK(hub.set_config(c) == 0);
    CHECK(hub.poll_once() == 1);
    Sample s{}; hub.read(&s, 1);
    CHECK(s.sensor_id == kSensorSimWave);
}
static void test_background_thread_and_listener() {
    HubCore hub(64, sim_only());
    std::atomic<int> ticks{0};
    hub.set_listener([&](size_t) { ++ticks; });
    Config c = hub.get_config(); c.period_ms = 10; hub.set_config(c);
    CHECK(!hub.running());
    hub.start(); hub.start();  // idempotent
    CHECK(hub.running());
    CHECK(wait_for([&] { return ticks >= 3; }));
    hub.stop(); hub.stop();    // idempotent
    CHECK(!hub.running());
    int after = ticks;
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK(ticks == after);     // really stopped
    CHECK(hub.stats().produced >= 3);
}
static void test_period_change_applies_live() {
    HubCore hub(64, sim_only());
    Config c = hub.get_config(); c.period_ms = 60000; hub.set_config(c);
    hub.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(hub.stats().produced == 0);
    c.period_ms = 10; hub.set_config(c);      // must interrupt the long wait
    CHECK(wait_for([&] { return hub.stats().produced >= 2; }, 2000));
}
static void test_text_commands() {
    HubCore hub(8, sim_only());
    CHECK(apply_command(hub, "period 20") == 0);
    CHECK(hub.get_config().period_ms == 20);
    CHECK(apply_command(hub, "mask 0x8\n") == 0);
    CHECK(hub.get_config().sensor_mask == 8);
    hub.poll_once();
    CHECK(apply_command(hub, "flush") == 0 && hub.available() == 0);
    CHECK(apply_command(hub, "start") == 0 && hub.running());
    CHECK(apply_command(hub, "stop") == 0 && !hub.running());
    CHECK(apply_command(hub, "") == EINVAL);
    CHECK(apply_command(hub, "bogus") == EINVAL);
    CHECK(apply_command(hub, "period") == EINVAL);
    CHECK(apply_command(hub, "period abc") == EINVAL);
    CHECK(apply_command(hub, "period 5") == EINVAL);  // out of range
}

int main() {
    RUN(test_poll_and_read); RUN(test_config_validation); RUN(test_overflow_drops_oldest);
    RUN(test_mask_filters_sensors); RUN(test_background_thread_and_listener);
    RUN(test_period_change_applies_live); RUN(test_text_commands);
    TEST_MAIN_END();
}
