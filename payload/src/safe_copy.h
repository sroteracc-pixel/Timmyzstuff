// safe_copy.h - copies memory WITHOUT ever crashing (stage D5b / D6).
//
// WHAT THIS IS (plain words)
//   Reading a wrong address normally kills the program. Here the memory is written into a pipe instead: the kernel checks every
//   address and just says "no" for a bad one, so nothing can crash.
//   Lesson from the real headset (stage D5): a write that is bigger than the pipe waits for a reader that never comes. So both ends of
//   the pipe never wait (O_NONBLOCK), the real pipe size is asked for and never exceeded, and the pipe is tested once before it is used.
#pragma once
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <cstring>
#include <mutex>

namespace tzscan {

const size_t kCopyChunk = 65536;

class CopyPipe {
public:
    // `scratch` (optional, kCopyChunk bytes) lets open() also test a full-size copy.
    bool open(size_t askedBytes, unsigned char* scratch) {
        int p[2];
        if (pipe(p) != 0) { err_ = errno; return false; }
        rd_ = p[0]; wr_ = p[1];
        fcntl(rd_, F_SETFL, fcntl(rd_, F_GETFL, 0) | O_NONBLOCK);
        fcntl(wr_, F_SETFL, fcntl(wr_, F_GETFL, 0) | O_NONBLOCK);
#ifdef F_SETPIPE_SZ
        fcntl(wr_, F_SETPIPE_SZ, static_cast<int>(askedBytes ? askedBytes : (1u << 17)));     // may be refused: then we use what we got
#else
        (void)askedBytes;
#endif
        long sz = 0;
#ifdef F_GETPIPE_SZ
        sz = fcntl(wr_, F_GETPIPE_SZ);
#endif
        if (sz < 4096) sz = 4096;                                      // unknown: assume one page
        pipeBytes_ = static_cast<size_t>(sz);
        cap_ = pipeBytes_ < kCopyChunk ? pipeBytes_ : kCopyChunk;
        cap_ &= ~static_cast<size_t>(4095);
        if (cap_ < 4096) cap_ = 4096;
        // self-test with our own memory: a small copy that must come back unchanged, then (if given) a full-size one
        unsigned char a[256], b[256];
        for (int i = 0; i < 256; ++i) { a[i] = static_cast<unsigned char>(i * 7 + 3); b[i] = 0; }
        bool ok = copy(reinterpret_cast<uintptr_t>(a), b, sizeof a) && std::memcmp(a, b, sizeof a) == 0;
        if (ok && scratch) ok = copy(reinterpret_cast<uintptr_t>(scratch), scratch, cap_);
        trouble_ = 0;                                                  // the self-test does not count
        if (!ok) { close(); return false; }
        return true;
    }
    void close() { if (rd_ >= 0) ::close(rd_); if (wr_ >= 0) ::close(wr_); rd_ = wr_ = -1; }
    size_t chunk() const { return cap_; }               // the biggest copy that fits the pipe
    size_t pipeBytes() const { return pipeBytes_; }
    int trouble() const { return trouble_; }            // copies that failed for a reason other than "bad address"
    int lastErrno() const { return err_; }

    // Copies n bytes from address `from` to `to`. False if any byte could not be read (nothing is promised about `to` then).
    bool copy(uintptr_t from, unsigned char* to, size_t n) {
        std::lock_guard<std::mutex> lock(mu_);
        if (rd_ < 0 || n == 0 || n > cap_) return false;
        ssize_t w;
        do { w = write(wr_, reinterpret_cast<const void*>(from), n); } while (w < 0 && errno == EINTR);
        if (w != static_cast<ssize_t>(n)) {
            if (w < 0 && errno != EFAULT) { ++trouble_; err_ = errno; }
            drain();                                                    // something in the range could not be read: throw away what was copied
            return false;
        }
        size_t got = 0;
        while (got < n) {
            const ssize_t r = read(rd_, to + got, n - got);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) { ++trouble_; err_ = r < 0 ? errno : 0; drain(); return false; }
            got += static_cast<size_t>(r);
        }
        return true;
    }

    // The other direction (stage D6): writes n bytes from `from` (our memory) to address `to`. The kernel does the copy into `to`, so an address
    // that is not mapped, or not writable, gives "no" instead of a crash. False if not every byte was written.
    bool put(uintptr_t to, const unsigned char* from, size_t n) {
        std::lock_guard<std::mutex> lock(mu_);
        if (rd_ < 0 || n == 0 || n > cap_) return false;
        ssize_t w;
        do { w = write(wr_, from, n); } while (w < 0 && errno == EINTR);
        if (w != static_cast<ssize_t>(n)) { ++trouble_; err_ = w < 0 ? errno : 0; drain(); return false; }
        size_t got = 0;
        while (got < n) {
            const ssize_t r = read(rd_, reinterpret_cast<void*>(to + got), n - got);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) { if (r == 0 || errno != EFAULT) { ++trouble_; err_ = r < 0 ? errno : 0; } drain(); return false; }      // EFAULT = "that address can not be written"
            got += static_cast<size_t>(r);
        }
        return true;
    }
private:
    void drain() { unsigned char d[4096]; for (int i = 0; i < 64; ++i) { const ssize_t r = read(rd_, d, sizeof d); if (r <= 0) break; } }
    int rd_ = -1, wr_ = -1; size_t cap_ = 4096, pipeBytes_ = 4096; int trouble_ = 0, err_ = 0; std::mutex mu_;
};


}  // namespace tzscan
