#include "sensorhub/sensor.hpp"

#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>

#include "sensorhub/abi.hpp"

namespace sensorhub {

// ---------- CPU temperature (sysfs thermal zone, value is milli-degC) ----------
CpuTempSensor::CpuTempSensor(std::string path) : path_(std::move(path)) {}
uint32_t CpuTempSensor::id() const { return kSensorCpuTemp; }
const char* CpuTempSensor::name() const { return sensor_name(kSensorCpuTemp); }
std::optional<int32_t> CpuTempSensor::read() {
    std::ifstream f(path_);
    long long v = 0;
    if (!(f >> v)) return std::nullopt;
    if (v > std::numeric_limits<int32_t>::max()) v = std::numeric_limits<int32_t>::max();
    if (v < std::numeric_limits<int32_t>::min()) v = std::numeric_limits<int32_t>::min();
    return static_cast<int32_t>(v);
}

// ---------- Load average (/proc/loadavg, first field) ----------
LoadAvgSensor::LoadAvgSensor(std::string path) : path_(std::move(path)) {}
uint32_t LoadAvgSensor::id() const { return kSensorLoadAvg; }
const char* LoadAvgSensor::name() const { return sensor_name(kSensorLoadAvg); }
std::optional<int32_t> LoadAvgSensor::read() {
    std::ifstream f(path_);
    double v = 0;
    if (!(f >> v) || v < 0) return std::nullopt;
    return static_cast<int32_t>(std::lround(v * 1000.0));
}

// ---------- Available memory (/proc/meminfo "MemAvailable", kB -> MiB*1000) ----------
MemAvailSensor::MemAvailSensor(std::string path) : path_(std::move(path)) {}
uint32_t MemAvailSensor::id() const { return kSensorMemFree; }
const char* MemAvailSensor::name() const { return sensor_name(kSensorMemFree); }
std::optional<int32_t> MemAvailSensor::read() {
    std::ifstream f(path_);
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("MemAvailable:", 0) == 0) {
            std::istringstream is(line.substr(13));
            long long kb = 0;
            if (!(is >> kb)) return std::nullopt;
            long long v = kb * 1000 / 1024;  // int64 math: no overflow for big RAM
            if (v > std::numeric_limits<int32_t>::max()) v = std::numeric_limits<int32_t>::max();
            return static_cast<int32_t>(v);
        }
    }
    return std::nullopt;
}

// ---------- Simulated sine wave ----------
uint32_t SimWaveSensor::id() const { return kSensorSimWave; }
const char* SimWaveSensor::name() const { return sensor_name(kSensorSimWave); }
std::optional<int32_t> SimWaveSensor::read() {
    constexpr double kTwoPi = 6.283185307179586;
    double v = 25000.0 + 10000.0 * std::sin(kTwoPi * static_cast<double>(tick_++) / 20.0);
    return static_cast<int32_t>(std::lround(v));
}

// ---------- Factory ----------
std::unique_ptr<ISensor> make_sensor(uint32_t id) {
    switch (id) {
        case kSensorCpuTemp: return std::make_unique<CpuTempSensor>();
        case kSensorLoadAvg: return std::make_unique<LoadAvgSensor>();
        case kSensorMemFree: return std::make_unique<MemAvailSensor>();
        case kSensorSimWave: return std::make_unique<SimWaveSensor>();
        default: return nullptr;
    }
}

std::vector<std::unique_ptr<ISensor>> make_default_sensors() {
    std::vector<std::unique_ptr<ISensor>> v;
    for (uint32_t id = 0; id < kSensorCount; ++id) v.push_back(make_sensor(id));
    return v;
}

}  // namespace sensorhub
