# Evaluation Guide (5–10 minute demo)

## Suggested flow
1. **(1 min) Problem & architecture** – show the diagram in `docs/ARCHITECTURE.md`. Say: layered design, HAL, producer/consumer, user-space char driver via CUSE because kernel code cannot be C++.
2. **(1 min) Repo tour** – `include/` ABI, `src/core`, `src/driver`, `src/cli`, `tests`, `docs`.
3. **(1 min) Build & test** – `./scripts/build.sh` → 3/3 tests pass.
4. **(4 min) Live demo** – `sudo ./build/sensorhub-driver -f`; second terminal: `ls -l /dev/sensorhub`, `shctl config`, `shctl read -n 5`, `shctl monitor`, `shctl send "period 50"`, `shctl stats`.
5. **(1 min) Edge cases** – blocking read + `Ctrl-C` (EINTR), `config --period 1` (EINVAL), `dropped` counter.
6. **(1 min) Limitations & future work.**

## Likely viva questions
| Question | Answer |
|---|---|
| Why not a kernel module? | Kernel is C-only; project rule is C++-only. CUSE gives a real char device from C++ and is an accepted user-space driver technique. |
| What is CUSE? | Kernel module (`cuse.ko`) exposing `/dev/cuse`; the daemon registers a device name and receives file operations as messages. |
| Where is `file_operations`? | `cuse_lowlevel_ops` table in `src/driver/main.cpp`. |
| How is blocking read implemented? | Request stored in `pending`; completed later by `service_waiters()`; the process sleeps in-kernel, like a wait queue. |
| How does poll wake up? | `shub_poll` stores the `fuse_pollhandle`; after new data `fuse_lowlevel_notify_poll` wakes the poller. |
| How does ioctl move data? | `CUSE_UNRESTRICTED_IOCTL` + `fuse_reply_ioctl_retry` (user-space `copy_to/from_user`). |
| Why overwrite-oldest? | Sensors need fresh data; `dropped` stat makes loss visible. |
| How do you avoid deadlock? | Fixed lock order, listener called without locks, `stop()` never under `Driver::m`; TSan clean. |
| How would you port to a kernel module? | Same ABI/ring/ioctl design maps 1:1: `cdev_add`, `wait_queue_head_t`, `kfifo`, `poll_wait`, `copy_to_user` (in C). |
| How to add a sensor? | Implement `ISensor`, add an id in `abi.hpp`, register in the factory. |
| Hardware-architecture angle? | HAL, FIFO buffering between sampler and consumer, fixed register-like ABI, real sysfs/procfs hardware sources. |
