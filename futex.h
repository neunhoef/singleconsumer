// futex.h - Linux implemation of C++ Futex class

#pragma once

#include <atomic>
#include <limits>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <sys/time.h>
#include <unistd.h>

namespace {
  static inline int futex(std::atomic<int>* uaddr, int futex_op, int val,
    const struct timespec *timeout, int *uaddr2, int val3) {
    return syscall(SYS_futex, reinterpret_cast<int*>(uaddr), futex_op, val,
                   timeout, uaddr, val3);
  }
}

class Futex {
 public:
  Futex() : _val() {}
  Futex(int val) : _val(val) {}

  std::atomic<int>& value() { return _val; }

  void wait(int expectedValue) {
    ::futex(&_val, FUTEX_WAIT_PRIVATE, expectedValue, nullptr, nullptr, 0);
  }

  void notifyOne() {
    ::futex(&_val, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
  }

  void notifyAll() {
    ::futex(&_val, FUTEX_WAKE_PRIVATE,
            std::numeric_limits<int>::max(), nullptr, nullptr, 0);
  }
 private:
  std::atomic<int> _val;
};

