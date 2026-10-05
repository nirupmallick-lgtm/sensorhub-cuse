# Linux Device-Driver Concepts Implemented

## Why a user-space (CUSE) driver?
The Linux kernel is written in C and does not support C++ in kernel space. Because
this capstone requires **C++ only**, the driver is implemented with **CUSE
(Character Device in Userspace)**: the in-kernel `cuse` module registers a real
character device (`/dev/sensorhub`) and forwards every file operation to our C++
daemon. To an application it is indistinguishable from a kernel char driver.
User-space drivers are also an industry technique (UIO, VFIO, libusb, FUSE).

## Concept mapping: kernel driver → this project
| Kernel-driver concept | Where it appears here |
|---|---|
| Character device, major/minor, `/dev` node | `cuse_lowlevel_main` + `DEVNAME=sensorhub`; node auto-created by CUSE/udev |
| `struct file_operations` | `cuse_lowlevel_ops` table in `main.cpp` (`open, release, read, write, ioctl, poll`) |
| `open` / `release` | `shub_open`, `shub_release` |
| `read` returning bytes, partial reads | `shub_read` returns whole 24-byte `Sample` records; `EINVAL` if buffer too small |
| Blocking I/O + wait queue | `pending` deque of blocked reads, completed in `service_waiters()` |
| `O_NONBLOCK` → `-EAGAIN` | `fi->flags & O_NONBLOCK` check |
| Signals → `-EINTR` | `fuse_req_interrupted()` handling |
| `poll` / `select` / `epoll`, `poll_wait()`, `wake_up()` | `shub_poll` + `fuse_lowlevel_notify_poll()` |
| `unlocked_ioctl`, `_IO/_IOR/_IOW` encoding | `abi.hpp` macros; `shub_ioctl` |
| `copy_to_user` / `copy_from_user` | `fuse_reply_ioctl_retry` + in/out buffers; `fuse_reply_buf` |
| `-ENOTTY` for unknown ioctl, `-EINVAL` for bad args | `default:` branch / `set_config` validation |
| `kfifo` / `circ_buf` | `RingBuffer<T>` |
| `kthread` + timer/workqueue | acquisition `std::thread` with `condition_variable::wait_for` |
| `mutex` / `spinlock` and lock ordering | see ARCHITECTURE.md → concurrency model |
| `module_init/exit`, `.release` cleanup | `main()` start-up and `shub_destroy` |
| Stable ABI between kernel and user space | fixed-size POD structs + `static_assert(sizeof)` |
| Driver statistics (`/proc`, sysfs style) | `SHUB_IOC_GET_STATS` |

## Hardware-architecture concepts
* **Hardware abstraction layer** – sensors behind `ISensor`; the rest of the system is hardware-agnostic.
* **Real hardware data paths** – thermal sensor via sysfs, scheduler/memory counters via procfs.
* **Producer/consumer buffering** between a periodic hardware sampler and asynchronous readers (the classic device-FIFO design).
* **Fixed-width little-endian records** – ABI layout chosen like a hardware register block.
