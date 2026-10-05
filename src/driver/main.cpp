// sensorhub-driver : user-space character device driver (CUSE) written in C++17.
//
// Creates /dev/sensorhub and implements the same entry points a kernel driver
// would put in `struct file_operations`:
//     open / release / read / write / unlocked_ioctl / poll
// Blocking reads, O_NONBLOCK, poll()/select()/epoll wake-ups and interrupt
// handling mirror kernel concepts (wait queues, poll_wait, -EAGAIN, -EINTR).
#define FUSE_USE_VERSION 31
#include <cuse_lowlevel.h>
#include <fuse_opt.h>

#include <fcntl.h>
#include <poll.h>
#include <sys/uio.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "sensorhub/abi.hpp"
#include "sensorhub/hub_core.hpp"

using namespace sensorhub;

namespace {

constexpr size_t kMaxBatch = 256;  // max records returned by one read()

// A blocked read(2) waiting for data  == an entry on a kernel wait queue.
struct PendingRead {
    fuse_req_t req;
    size_t max_records;
};

struct Driver {
    std::unique_ptr<HubCore> core;
    std::mutex m;                          // protects pending + pollers
    std::deque<PendingRead> pending;       // "wait queue" of blocked readers
    std::vector<fuse_pollhandle*> pollers; // "poll_wait" registrations
};

Driver g;

// Complete blocked readers and wake pollers. Called from the acquisition thread
// after every tick, and also reaps readers whose process was interrupted.
void service_waiters() {
    std::lock_guard<std::mutex> lk(g.m);
    while (!g.pending.empty()) {
        PendingRead p = g.pending.front();
        if (fuse_req_interrupted(p.req)) {            // signal -> -EINTR
            g.pending.pop_front();
            fuse_reply_err(p.req, EINTR);
            continue;
        }
        if (g.core->available() == 0) break;
        std::vector<Sample> buf(p.max_records);
        size_t got = g.core->read(buf.data(), buf.size());
        if (got == 0) break;
        g.pending.pop_front();
        fuse_reply_buf(p.req, reinterpret_cast<const char*>(buf.data()), got * sizeof(Sample));
    }
    if (!g.pollers.empty() && g.core->available() > 0) {
        for (auto* ph : g.pollers) {
            fuse_lowlevel_notify_poll(ph);        // wake_up(&wait_queue) equivalent
            fuse_pollhandle_destroy(ph);
        }
        g.pollers.clear();
    }
}

// ----------------------------- file_operations -----------------------------
void shub_open(fuse_req_t req, fuse_file_info* fi) {
    fi->nonseekable = 1;
    fuse_reply_open(req, fi);
}

void shub_release(fuse_req_t req, fuse_file_info*) { fuse_reply_err(req, 0); }

void shub_read(fuse_req_t req, size_t size, off_t, fuse_file_info* fi) {
    size_t n = std::min(size / sizeof(Sample), kMaxBatch);
    if (n == 0) { fuse_reply_err(req, EINVAL); return; }  // buffer smaller than 1 record

    std::lock_guard<std::mutex> lk(g.m);
    std::vector<Sample> buf(n);
    size_t got = g.core->read(buf.data(), n);
    if (got > 0) {
        fuse_reply_buf(req, reinterpret_cast<const char*>(buf.data()), got * sizeof(Sample));
        return;
    }
    if (fi->flags & O_NONBLOCK) { fuse_reply_err(req, EAGAIN); return; }
    g.pending.push_back({req, n});  // block: reply later from service_waiters()
}

void shub_write(fuse_req_t req, const char* buf, size_t size, off_t, fuse_file_info*) {
    std::string line(buf, std::min<size_t>(size, 128));
    int rc = apply_command(*g.core, line);
    if (rc != 0) fuse_reply_err(req, rc);
    else fuse_reply_write(req, size);
}

void shub_poll(fuse_req_t req, fuse_file_info*, fuse_pollhandle* ph) {
    std::lock_guard<std::mutex> lk(g.m);
    unsigned revents = POLLOUT;  // writes (commands) never block
    if (g.core->available() > 0) revents |= POLLIN;
    if (ph) {
        if (revents & POLLIN) fuse_pollhandle_destroy(ph);
        else g.pollers.push_back(ph);
    }
    fuse_reply_poll(req, revents);
}

// With CUSE_UNRESTRICTED_IOCTL the server asks the kernel to copy user memory
// with fuse_reply_ioctl_retry() - the user-space version of copy_from/to_user().
template <typename T>
void reply_out(fuse_req_t req, void* arg, size_t out_bufsz, const T& v) {
    if (out_bufsz < sizeof(T)) {
        iovec iov{arg, sizeof(T)};
        fuse_reply_ioctl_retry(req, nullptr, 0, &iov, 1);
        return;
    }
    fuse_reply_ioctl(req, 0, &v, sizeof(T));
}

void shub_ioctl(fuse_req_t req, int cmd, void* arg, fuse_file_info*, unsigned flags,
                const void* in_buf, size_t in_bufsz, size_t out_bufsz) {
    if (flags & FUSE_IOCTL_COMPAT) { fuse_reply_err(req, ENOSYS); return; }

    switch (static_cast<unsigned>(cmd)) {
        case SHUB_IOC_GET_CONFIG:
            reply_out(req, arg, out_bufsz, g.core->get_config());
            return;
        case SHUB_IOC_GET_STATS:
            reply_out(req, arg, out_bufsz, g.core->stats());
            return;
        case SHUB_IOC_SET_CONFIG: {
            if (in_bufsz < sizeof(Config)) {
                iovec iov{arg, sizeof(Config)};
                fuse_reply_ioctl_retry(req, &iov, 1, nullptr, 0);
                return;
            }
            Config c;
            std::memcpy(&c, in_buf, sizeof c);
            int rc = g.core->set_config(c);
            if (rc) fuse_reply_err(req, rc);
            else fuse_reply_ioctl(req, 0, nullptr, 0);
            return;
        }
        case SHUB_IOC_START: g.core->start(); fuse_reply_ioctl(req, 0, nullptr, 0); return;
        case SHUB_IOC_STOP:  g.core->stop();  fuse_reply_ioctl(req, 0, nullptr, 0); return;
        case SHUB_IOC_FLUSH: g.core->flush(); fuse_reply_ioctl(req, 0, nullptr, 0); return;
        default: fuse_reply_err(req, ENOTTY); return;  // standard "unknown ioctl" errno
    }
}

void shub_destroy(void*) {
    if (g.core) g.core->stop();
}

void usage(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s [options]\n"
        "  -f                run in foreground (recommended)\n"
        "  -d                FUSE debug output (implies -f)\n"
        "  --capacity=N      ring-buffer capacity in samples (default %u)\n"
        "  --no-autostart    do not start sampling at launch\n"
        "  -h, --help        show this help\n"
        "Needs root and the 'cuse' kernel module (sudo modprobe cuse).\n",
        prog, kDefaultCapacity);
}

}  // namespace

int main(int argc, char** argv) {
    size_t capacity = kDefaultCapacity;
    bool autostart = true;
    std::vector<char*> fwd;
    fwd.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else if (a.rfind("--capacity=", 0) == 0) {
            long v = std::strtol(a.c_str() + 11, nullptr, 10);
            if (v <= 0 || v > 1000000) { std::fprintf(stderr, "invalid --capacity\n"); return 2; }
            capacity = static_cast<size_t>(v);
        } else if (a == "--no-autostart") autostart = false;
        else fwd.push_back(argv[i]);
    }

    if (access("/dev/cuse", R_OK | W_OK) != 0) {
        std::fprintf(stderr,
            "error: cannot open /dev/cuse (%s).\n"
            "       Run as root and load the module:  sudo modprobe cuse\n", std::strerror(errno));
        return 1;
    }

    g.core = std::make_unique<HubCore>(capacity, make_default_sensors());
    g.core->set_listener([](size_t) { service_waiters(); });
    if (autostart) g.core->start();

    static const cuse_lowlevel_ops ops = [] {
        cuse_lowlevel_ops o{};
        o.open = shub_open;
        o.release = shub_release;
        o.read = shub_read;
        o.write = shub_write;
        o.poll = shub_poll;
        o.ioctl = shub_ioctl;
        o.destroy = shub_destroy;
        return o;
    }();

    std::string devname = std::string("DEVNAME=") + kDeviceName;
    const char* dev_info_argv[] = {devname.c_str()};
    cuse_info ci{};
    ci.dev_info_argc = 1;
    ci.dev_info_argv = dev_info_argv;
    ci.flags = CUSE_UNRESTRICTED_IOCTL;

    std::fprintf(stderr, "sensorhub-driver: creating %s (capacity=%zu, autostart=%d)\n",
                 kDevicePath, capacity, autostart ? 1 : 0);
    int rc = cuse_lowlevel_main(static_cast<int>(fwd.size()), fwd.data(), &ci, &ops, nullptr);
    g.core->stop();
    return rc;
}
