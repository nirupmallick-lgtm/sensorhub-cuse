# SensorHub — A C++17 Character Device Driver with a Layered Sensor Architecture

> Capstone Project · Language: **C++17 only** · OS: **Linux** · Topic: **Software/Hardware Architecture + Linux Device Drivers**

**Author:** `<YOUR NAME>` · **Batch/Trainer:** `<BATCH / TRAINER>` · **Date:** October 2026

## 1. Overview
SensorHub creates a real Linux character device, **`/dev/sensorhub`**, that streams
timestamped sensor samples (CPU temperature, load average, available memory and a
simulated wave). It is built as a layered architecture: *application → ioctl/file ABI →
driver layer → acquisition core → hardware-abstraction layer → hardware/system sources*.

Because the Linux kernel does not allow C++, the driver runs as a **user-space driver
using CUSE** (Character Device in Userspace). The kernel registers a genuine char
device and routes `open/read/write/ioctl/poll` to the C++ daemon — the same entry
points as a kernel `file_operations` table.

## 2. Requirement compliance
| Requirement | How it is met |
|---|---|
| C++ only | 100 % C++17 (plus CMake/Bash for build/demo scripts) |
| Linux only | Uses `/dev`, CUSE, sysfs, procfs, `ioctl`, `poll` |
| Linux device-driver concepts | char device, `file_operations`, blocking I/O + wait queue, `O_NONBLOCK`/`EAGAIN`, `EINTR`, `poll`, `ioctl` numbers + user copy, ring buffer — see [`docs/DRIVER_CONCEPTS.md`](docs/DRIVER_CONCEPTS.md) |
| Software/hardware architecture | Layered design, HAL (`ISensor`), producer/consumer, observer, factory — see [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) |
| GitHub submission | Structured repo, README, docs, scripts, CI — see [`docs/SUBMISSION.md`](docs/SUBMISSION.md) |
| Evaluation | Demo script + viva Q&A — [`docs/EVALUATION_GUIDE.md`](docs/EVALUATION_GUIDE.md) |

## 3. Architecture
```
 shctl / apps ── open · read · write · ioctl · poll
        │
   VFS /dev/sensorhub  →  cuse.ko  →  /dev/cuse
        │
 ┌──────▼─────────────────────── sensorhub-driver (C++17) ───────────────┐
 │ Driver layer : file_operations handlers, wait-queue, poll wake-ups    │
 │ Core layer   : HubCore  (acquisition thread + RingBuffer + config)    │
 │ HAL          : ISensor → CpuTemp │ LoadAvg │ MemAvail │ SimWave      │
 └──────────────────────────────┬────────────────────────────────────────┘
              /sys/class/thermal · /proc/loadavg · /proc/meminfo · simulation
```
Full diagrams, data flow and concurrency model: [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## 4. Project structure
```
sensorhub-cuse/
├── CMakeLists.txt
├── README.md  LICENSE  .gitignore
├── include/sensorhub/
│   ├── abi.hpp          # public ABI: structs + ioctl numbers
│   ├── ring_buffer.hpp  # thread-safe overwrite-oldest FIFO
│   ├── sensor.hpp       # ISensor + implementations + factory
│   └── hub_core.hpp     # acquisition engine + text commands
├── src/
│   ├── core/            # hub_core.cpp, sensors.cpp
│   ├── driver/main.cpp  # CUSE driver daemon (/dev/sensorhub)
│   └── cli/shctl.cpp    # command-line client
├── tests/               # 3 unit-test suites (ctest)
├── scripts/             # setup.sh  build.sh  demo.sh
├── docs/                # ARCHITECTURE, DRIVER_CONCEPTS, USER_GUIDE, TESTING, EVALUATION_GUIDE, SUBMISSION
└── .github/workflows/ci.yml
```

## 5. Quick start
```bash
git clone https://github.com/<your-username>/sensorhub-cuse.git && cd sensorhub-cuse
./scripts/setup.sh        # apt packages + sudo modprobe cuse
./scripts/build.sh        # build + run unit tests

# Terminal 1
sudo ./build/sensorhub-driver -f
# Terminal 2
ls -l /dev/sensorhub
sudo ./build/shctl read -n 5
sudo ./build/shctl monitor
```
Automated end-to-end demo: `sudo ./scripts/demo.sh`.
Full instructions & troubleshooting: [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md).

## 6. Device interface
| Call | Behaviour |
|---|---|
| `read(fd, buf, n)` | whole 24-byte `Sample` records; blocks when empty; `EAGAIN` with `O_NONBLOCK` |
| `write(fd, "period 50")` | text commands `start`/`stop`/`flush`/`period <ms>`/`mask <n>` |
| `ioctl(SHUB_IOC_GET/SET_CONFIG)` | read/modify mask (bits 0–3) and period (10–60000 ms) |
| `ioctl(SHUB_IOC_GET_STATS)` | produced / consumed / dropped / fill / capacity |
| `ioctl(SHUB_IOC_START/STOP/FLUSH)` | control acquisition |
| `poll/select/epoll` | `POLLIN` when data is buffered |

## 7. Sample output
```
$ shctl read -n 4
[  4821.337 s] seq=0      SIM_WAVE      25.000 a.u.
[  4821.337 s] seq=1      LOAD_AVG       0.520 load
[  4821.437 s] seq=2      SIM_WAVE      28.090 a.u.
[  4821.437 s] seq=3      LOAD_AVG       0.520 load
$ shctl stats
produced : 412
consumed : 4
dropped  : 156
fill     : 256 / 256
```
*(Replace with your own real output/screenshots from `scripts/demo.sh`.)*

## 8. Testing
`ctest --test-dir build --output-on-failure` → 3 suites: ring buffer (incl. multithreaded accounting), sensors (parsing/error handling), hub core (config validation, overflow policy, threading, command parser). Also verified clean under ThreadSanitizer and `-Wall -Wextra -Wpedantic`. Details: [`docs/TESTING.md`](docs/TESTING.md).

## 9. Design decisions
* **CUSE instead of an in-kernel module** — kernel code must be C; CUSE satisfies "C++ only" while still being a real char device.
* **Overwrite-oldest buffer + `dropped` counter** — freshest data wins, loss is visible.
* **Core independent of FUSE** — makes the logic unit-testable without root.
* **Fixed-size POD ABI with `static_assert`** — stable binary contract like a hardware register map.
* **Unavailable sensors are skipped, never faked** — correctness over completeness.

## 10. Limitations & future work
* Single shared buffer: each sample goes to one reader (per-open buffers would give broadcast semantics).
* CUSE adds user/kernel round-trip latency versus an in-kernel driver.
* Possible extensions: I²C/SPI sensors through `/dev/i2c-*`, per-open subscriptions, epoll benchmark, systemd unit + udev rule, Prometheus exporter.

## 11. License
MIT — see [LICENSE](LICENSE).
