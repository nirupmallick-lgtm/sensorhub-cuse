// sensor.hpp - Hardware-abstraction layer: one ISensor per data source.
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sensorhub {

class ISensor {
public:
    virtual ~ISensor() = default;
    virtual uint32_t id() const = 0;
    virtual const char* name() const = 0;
    // Returns value*1000, or nullopt if the source is currently unavailable.
    virtual std::optional<int32_t> read() = 0;
};

class CpuTempSensor final : public ISensor {
public:
    explicit CpuTempSensor(std::string path = "/sys/class/thermal/thermal_zone0/temp");
    uint32_t id() const override;
    const char* name() const override;
    std::optional<int32_t> read() override;
private:
    std::string path_;
};

class LoadAvgSensor final : public ISensor {
public:
    explicit LoadAvgSensor(std::string path = "/proc/loadavg");
    uint32_t id() const override;
    const char* name() const override;
    std::optional<int32_t> read() override;
private:
    std::string path_;
};

class MemAvailSensor final : public ISensor {
public:
    explicit MemAvailSensor(std::string path = "/proc/meminfo");
    uint32_t id() const override;
    const char* name() const override;
    std::optional<int32_t> read() override;
private:
    std::string path_;
};

// Deterministic sine wave; works on any machine (VMs without thermal zones).
class SimWaveSensor final : public ISensor {
public:
    uint32_t id() const override;
    const char* name() const override;
    std::optional<int32_t> read() override;
private:
    uint64_t tick_ = 0;
};

std::unique_ptr<ISensor> make_sensor(uint32_t id);  // factory
std::vector<std::unique_ptr<ISensor>> make_default_sensors();

}  // namespace sensorhub
