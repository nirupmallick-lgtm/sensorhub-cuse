# Testing

## Automated unit tests (`ctest`)
| Suite | Covers |
|---|---|
| `test_ring_buffer` | FIFO order, wrap-around, overwrite-oldest, drop counting, invalid capacity, 2-producer/1-consumer accounting |
| `test_sensors` | sysfs/procfs parsing using temp files, missing-source handling, 64-bit overflow-safe maths, factory |
| `test_hub_core` | tick/read, config validation (`EINVAL`), overflow policy, sensor mask, background thread + listener, idempotent start/stop, live period change, text-command parser incl. bad input |

```bash
ctest --test-dir build --output-on-failure
```
Also verified with ThreadSanitizer (`-fsanitize=thread`): no data races, and compiled with `-Wall -Wextra -Wpedantic` with zero warnings.

## System test (needs root + CUSE)
`sudo ./scripts/demo.sh` exercises: ioctl get/set/stats/start/stop/flush, blocking read,
write text interface, poll-based monitor, and error paths (`EINVAL`).

## Manual checklist
| # | Action | Expected |
|---|---|---|
| 1 | start driver, `ls -l /dev/sensorhub` | `c` (char device) node |
| 2 | `shctl read -n 5` | 5 formatted samples |
| 3 | `shctl stop; shctl read -n 1` | blocks (no data); `Ctrl-C` ends it cleanly |
| 4 | `shctl start` in another terminal while 3 blocks | blocked read completes |
| 5 | `shctl monitor` | live samples via poll |
| 6 | `shctl config --period 1` | error (EINVAL) |
| 7 | leave running 10 s unread, `shctl stats` | `dropped > 0`, `fill == capacity` |
| 8 | `Ctrl-C` driver | `/dev/sensorhub` removed |
