#!/usr/bin/env bash
# Install build dependencies (Debian/Ubuntu) and load the CUSE kernel module.
set -euo pipefail
sudo apt-get update
sudo apt-get install -y g++ cmake make pkg-config libfuse3-dev fuse3
sudo modprobe cuse
ls -l /dev/cuse
echo "Setup complete."
