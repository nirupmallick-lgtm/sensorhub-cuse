// abi.hpp - Public user/driver ABI of the SensorHub character device.
// Shared by the driver daemon and every client (shctl, tests, your own apps).
#pragma once
#include <cstdint>
#include <linux/ioctl.h>

namespace sensorhub {

inline constexpr const char* kDeviceName = "sensorhub";
inline constexpr const char* kDevicePath = "/dev/sensorhub";

enum SensorId : uint32_t {
    kSensorCpuTemp = 0,  // /sys/class/thermal (milli-degC)
    kSensorLoadAvg = 1,  // /proc/loadavg      (load * 1000)
    kSensorMemFree = 2,  // /proc/meminfo      (MiB * 1000)
    kSensorSimWave = 3,  // deterministic simulated sensor
    kSensorCount   = 4
};

inline constexpr uint32_t kAllSensorsMask = (1u << kSensorCount) - 1u;
inline constexpr uint32_t kMinPeriodMs = 10;
inline constexpr uint32_t kMaxPeriodMs = 60000;
inline constexpr uint32_t kDefaultPeriodMs = 100;
inline constexpr uint32_t kDefaultCapacity = 256;

inline const char* sensor_name(uint32_t id) {
    switch (id) {
        case kSensorCpuTemp: return "CPU_TEMP";
        case kSensorLoadAvg: return "LOAD_AVG";
        case kSensorMemFree: return "MEM_AVAIL";
        case kSensorSimWave: return "SIM_WAVE";
        default: return "UNKNOWN";
    }
}
inline const char* sensor_unit(uint32_t id) {
    switch (id) {
        case kSensorCpuTemp: return "degC";
        case kSensorLoadAvg: return "load";
        case kSensorMemFree: return "MiB";
        case kSensorSimWave: return "a.u.";
        default: return "";
    }
}

// One record returned by read(2). Fixed 24-byte layout (no padding).
struct Sample {
    uint64_t timestamp_ns;  // CLOCK_MONOTONIC
    uint32_t sensor_id;     // SensorId
    uint32_t sequence;      // global, monotonically increasing
    int32_t  value_milli;   // value * 1000
    uint32_t reserved;
};
static_assert(sizeof(Sample) == 24, "ABI: Sample must be 24 bytes");

struct Config {
    uint32_t sensor_mask;  // bit i enables sensor i
    uint32_t period_ms;    // sampling period
    uint32_t running;      // read-only: 1 if acquisition thread is running
    uint32_t reserved;
};
static_assert(sizeof(Config) == 16, "ABI: Config must be 16 bytes");

struct Stats {
    uint64_t produced;  // samples generated
    uint64_t consumed;  // samples delivered to readers
    uint64_t dropped;   // samples overwritten because the buffer was full
    uint32_t fill;      // samples currently buffered
    uint32_t capacity;  // ring-buffer capacity
};
static_assert(sizeof(Stats) == 32, "ABI: Stats must be 32 bytes");

}  // namespace sensorhub

// ioctl command numbers (see Documentation/userspace-api/ioctl/ioctl-number.rst)
#define SHUB_IOC_MAGIC      'S'
#define SHUB_IOC_GET_CONFIG _IOR(SHUB_IOC_MAGIC, 0x01, ::sensorhub::Config)
#define SHUB_IOC_SET_CONFIG _IOW(SHUB_IOC_MAGIC, 0x02, ::sensorhub::Config)
#define SHUB_IOC_GET_STATS  _IOR(SHUB_IOC_MAGIC, 0x03, ::sensorhub::Stats)
#define SHUB_IOC_FLUSH      _IO(SHUB_IOC_MAGIC, 0x04)
#define SHUB_IOC_START      _IO(SHUB_IOC_MAGIC, 0x05)
#define SHUB_IOC_STOP       _IO(SHUB_IOC_MAGIC, 0x06)
