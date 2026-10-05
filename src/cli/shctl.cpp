// shctl - command-line client for /dev/sensorhub (exercises read/write/ioctl/poll).
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "sensorhub/abi.hpp"

using namespace sensorhub;

static int open_dev(int flags) {
    const char* p = std::getenv("SHUB_DEV");
    if (!p) p = kDevicePath;
    int fd = open(p, flags);
    if (fd < 0) {
        std::fprintf(stderr, "shctl: cannot open %s: %s\n"
                             "       (is sensorhub-driver running? permissions?)\n", p, std::strerror(errno));
        std::exit(1);
    }
    return fd;
}

static void print_sample(const Sample& s) {
    std::printf("[%10.3f s] seq=%-6u %-10s %10.3f %s\n",
                static_cast<double>(s.timestamp_ns) / 1e9, s.sequence, sensor_name(s.sensor_id),
                s.value_milli / 1000.0, sensor_unit(s.sensor_id));
}

static void usage() {
    std::puts(
        "Usage: shctl <command> [args]\n"
        "  stats                         show ring-buffer statistics (ioctl GET_STATS)\n"
        "  config                        show configuration        (ioctl GET_CONFIG)\n"
        "  config --mask M --period MS   change configuration      (ioctl SET_CONFIG)\n"
        "  start | stop | flush          control acquisition       (ioctl START/STOP/FLUSH)\n"
        "  send \"<text command>\"         use the write(2) text interface\n"
        "  read [-n N]                   blocking read of N samples (default 10)\n"
        "  monitor [-n N]                poll(2)-driven live view (N samples, default: forever)\n"
        "Environment: SHUB_DEV overrides the device path (default /dev/sensorhub).");
}

static int do_ioctl(int fd, unsigned long req, void* arg = nullptr) {
    int rc = ioctl(fd, req, arg);
    if (rc < 0) std::fprintf(stderr, "shctl: ioctl failed: %s\n", std::strerror(errno));
    return rc;
}

static long parse_n(int argc, char** argv, long def) {
    for (int i = 2; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "-n")) return std::strtol(argv[i + 1], nullptr, 10);
    return def;
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    std::string cmd = argv[1];

    if (cmd == "-h" || cmd == "--help" || cmd == "help") { usage(); return 0; }

    if (cmd == "stats") {
        int fd = open_dev(O_RDWR);
        Stats s{};
        if (do_ioctl(fd, SHUB_IOC_GET_STATS, &s) < 0) return 1;
        std::printf("produced : %llu\nconsumed : %llu\ndropped  : %llu\nfill     : %u / %u\n",
                    (unsigned long long)s.produced, (unsigned long long)s.consumed,
                    (unsigned long long)s.dropped, s.fill, s.capacity);
        return 0;
    }

    if (cmd == "config") {
        int fd = open_dev(O_RDWR);
        Config c{};
        if (do_ioctl(fd, SHUB_IOC_GET_CONFIG, &c) < 0) return 1;
        bool change = false;
        for (int i = 2; i + 1 < argc; i += 2) {
            if (!std::strcmp(argv[i], "--mask"))        { c.sensor_mask = std::strtoul(argv[i + 1], nullptr, 0); change = true; }
            else if (!std::strcmp(argv[i], "--period")) { c.period_ms = std::strtoul(argv[i + 1], nullptr, 0); change = true; }
            else { usage(); return 1; }
        }
        if (change && do_ioctl(fd, SHUB_IOC_SET_CONFIG, &c) < 0) return 1;
        if (change && do_ioctl(fd, SHUB_IOC_GET_CONFIG, &c) < 0) return 1;
        std::printf("mask    : 0x%X\nperiod  : %u ms\nrunning : %s\n", c.sensor_mask, c.period_ms,
                    c.running ? "yes" : "no");
        return 0;
    }

    if (cmd == "start" || cmd == "stop" || cmd == "flush") {
        int fd = open_dev(O_RDWR);
        unsigned long r = cmd == "start" ? SHUB_IOC_START : cmd == "stop" ? SHUB_IOC_STOP : SHUB_IOC_FLUSH;
        return do_ioctl(fd, r) < 0 ? 1 : 0;
    }

    if (cmd == "send") {
        if (argc < 3) { usage(); return 1; }
        int fd = open_dev(O_RDWR);
        std::string line = argv[2];
        if (write(fd, line.data(), line.size()) < 0) {
            std::fprintf(stderr, "shctl: write failed: %s\n", std::strerror(errno));
            return 1;
        }
        return 0;
    }

    if (cmd == "read") {
        long want = parse_n(argc, argv, 10);
        int fd = open_dev(O_RDWR);
        long got = 0;
        while (got < want) {
            Sample buf[32];
            size_t n = std::min<long>(want - got, 32);
            ssize_t r = read(fd, buf, n * sizeof(Sample));  // blocks until data arrives
            if (r < 0) { if (errno == EINTR) continue; std::perror("read"); return 1; }
            for (ssize_t i = 0; i < r / (ssize_t)sizeof(Sample); ++i) print_sample(buf[i]);
            got += r / sizeof(Sample);
        }
        return 0;
    }

    if (cmd == "monitor") {
        long want = parse_n(argc, argv, -1);
        int fd = open_dev(O_RDWR | O_NONBLOCK);
        long got = 0;
        while (want < 0 || got < want) {
            pollfd p{fd, POLLIN, 0};
            int pr = poll(&p, 1, 2000);
            if (pr < 0) { if (errno == EINTR) continue; std::perror("poll"); return 1; }
            if (pr == 0) { std::fprintf(stderr, "(no data for 2 s)\n"); continue; }
            Sample buf[32];
            ssize_t r = read(fd, buf, sizeof buf);
            if (r < 0) { if (errno == EAGAIN || errno == EINTR) continue; std::perror("read"); return 1; }
            for (ssize_t i = 0; i < r / (ssize_t)sizeof(Sample); ++i) { print_sample(buf[i]); ++got; }
            std::fflush(stdout);
        }
        return 0;
    }

    usage();
    return 1;
}
