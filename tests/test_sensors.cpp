#include <unistd.h>
#include <fstream>
#include <string>
#include "check.hpp"
#include "sensorhub/abi.hpp"
#include "sensorhub/sensor.hpp"
using namespace sensorhub;

static std::string tmpfile_with(const std::string& tag, const std::string& content) {
    std::string p = "/tmp/shub_test_" + std::to_string(getpid()) + "_" + tag;
    std::ofstream(p) << content;
    return p;
}

static void test_cpu_temp() {
    auto p = tmpfile_with("temp", "45000\n");
    CpuTempSensor s(p);
    CHECK(s.read().value() == 45000);
    unlink(p.c_str());
    CHECK(!CpuTempSensor("/nonexistent/path").read().has_value());
}
static void test_loadavg() {
    auto p = tmpfile_with("load", "0.52 0.58 0.59 1/467 12345\n");
    CHECK(LoadAvgSensor(p).read().value() == 520);
    unlink(p.c_str());
}
static void test_mem_avail() {
    auto p = tmpfile_with("mem", "MemTotal:   16000000 kB\nMemFree: 1 kB\nMemAvailable:    2048000 kB\n");
    CHECK(MemAvailSensor(p).read().value() == 2000000);  // 2000 MiB * 1000
    unlink(p.c_str());
    auto q = tmpfile_with("mem2", "MemTotal: 1 kB\n");
    CHECK(!MemAvailSensor(q).read().has_value());
    unlink(q.c_str());
}
static void test_sim_wave() {
    SimWaveSensor s;
    CHECK(s.read().value() == 25000);  // sin(0)
    for (int i = 0; i < 100; ++i) {
        int v = s.read().value();
        CHECK(v >= 15000 && v <= 35000);
    }
}
static void test_factory() {
    for (uint32_t i = 0; i < kSensorCount; ++i) CHECK(make_sensor(i) && make_sensor(i)->id() == i);
    CHECK(make_sensor(99) == nullptr);
    CHECK(make_default_sensors().size() == kSensorCount);
}

int main() {
    RUN(test_cpu_temp); RUN(test_loadavg); RUN(test_mem_avail);
    RUN(test_sim_wave); RUN(test_factory);
    TEST_MAIN_END();
}
