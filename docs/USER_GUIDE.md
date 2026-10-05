# User Guide

## 1. Requirements
* Linux (Ubuntu 22.04+ tested design), root access, kernel with `CONFIG_CUSE` (default on Ubuntu/Debian/Fedora)
* `g++` (C++17), `cmake` ≥ 3.16, `pkg-config`, `libfuse3-dev`

```bash
./scripts/setup.sh        # installs packages and runs: sudo modprobe cuse
```

## 2. Build & test
```bash
./scripts/build.sh        # cmake + make + ctest
```
Outputs: `build/sensorhub-driver`, `build/shctl`, and three test executables.

## 3. Run the driver
```bash
sudo modprobe cuse
sudo ./build/sensorhub-driver -f            # terminal 1 (foreground)
ls -l /dev/sensorhub                        # terminal 2 – character device exists
sudo chmod 666 /dev/sensorhub               # optional: allow non-root clients
```
Options: `--capacity=N`, `--no-autostart`, `-d` (debug). Stop with `Ctrl-C`;
the node disappears automatically.

## 4. Use it
```bash
./build/shctl config                         # ioctl GET_CONFIG
./build/shctl config --mask 0xF --period 200 # ioctl SET_CONFIG (mask bits: 0 temp,1 load,2 mem,3 sim)
./build/shctl read -n 10                     # blocking read(2)
./build/shctl monitor                        # poll(2) live view, Ctrl-C to quit
./build/shctl send "period 50"               # write(2) text command
./build/shctl stats                          # ioctl GET_STATS
./build/shctl stop / start / flush           # ioctl control
echo "mask 8" | sudo tee /dev/sensorhub      # plain shell works too
sudo cat /dev/sensorhub | xxd | head         # raw binary records
```
One-shot automated demonstration: `sudo ./scripts/demo.sh`

## 5. Programming interface
Include `include/sensorhub/abi.hpp`.
```cpp
int fd = open("/dev/sensorhub", O_RDWR);
sensorhub::Sample s;  read(fd, &s, sizeof s);          // blocks for one sample
sensorhub::Config c;  ioctl(fd, SHUB_IOC_GET_CONFIG, &c);
c.period_ms = 500;    ioctl(fd, SHUB_IOC_SET_CONFIG, &c);
```
| Operation | Behaviour |
|---|---|
| `read` | Returns 1..256 whole `Sample` records; blocks if empty, `EAGAIN` with `O_NONBLOCK`, `EINVAL` if buffer < 24 bytes |
| `write` | Text commands: `start`, `stop`, `flush`, `period <ms>`, `mask <n>`; `EINVAL` otherwise |
| `ioctl` | `GET_CONFIG`, `SET_CONFIG`, `GET_STATS`, `FLUSH`, `START`, `STOP`; unknown → `ENOTTY` |
| `poll` | `POLLIN` when samples are buffered, `POLLOUT` always |

Valid config: `period_ms` 10–60000; `sensor_mask` non-zero, bits 0–3.
The buffer is shared: each sample is delivered to exactly one reader.

## 6. Troubleshooting
| Symptom | Fix |
|---|---|
| `cannot open /dev/cuse` | `sudo modprobe cuse`, run driver with `sudo` |
| `shctl: Permission denied` | `sudo chmod 666 /dev/sensorhub` or run with sudo |
| No `CPU_TEMP` samples | VM/container without thermal zone – the sensor is skipped by design; use `SIM_WAVE` |
| Node stays after crash | `sudo pkill sensorhub-driver`; it is removed when the daemon exits |
| Build: fuse3 not found | `sudo apt install libfuse3-dev pkg-config` |
