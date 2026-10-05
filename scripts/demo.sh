#!/usr/bin/env bash
# End-to-end demo: starts the driver, exercises every file operation, stops it.
# Usage: sudo ./scripts/demo.sh        (run scripts/build.sh first)
set -euo pipefail
cd "$(dirname "$0")/.."
B=build
[[ $EUID -eq 0 ]] || { echo "Run as root: sudo $0"; exit 1; }
modprobe cuse
$B/sensorhub-driver -f --capacity=128 &
DRV=$!
trap 'kill $DRV 2>/dev/null || true; wait $DRV 2>/dev/null || true' EXIT
for _ in $(seq 1 50); do [[ -e /dev/sensorhub ]] && break; sleep 0.1; done
[[ -e /dev/sensorhub ]] || { echo "device node did not appear"; exit 1; }
ls -l /dev/sensorhub

echo; echo "== 1. ioctl GET_CONFIG ==";            $B/shctl config
echo; echo "== 2. ioctl SET_CONFIG (sim+load, 200 ms) =="; $B/shctl config --mask 0xA --period 200
echo; echo "== 3. blocking read(2) of 6 samples =="; $B/shctl read -n 6
echo; echo "== 4. write(2) text command: 'period 50' =="; $B/shctl send "period 50"; $B/shctl config
echo; echo "== 5. poll(2) live monitor, 8 samples =="; $B/shctl monitor -n 8
echo; echo "== 6. ioctl GET_STATS ==";             $B/shctl stats
echo; echo "== 7. stop + flush =="; $B/shctl stop; $B/shctl flush; $B/shctl stats
echo; echo "== 8. invalid input is rejected (EINVAL) =="; $B/shctl send "period 1" || echo "rejected as expected"
echo; echo "Demo finished."
