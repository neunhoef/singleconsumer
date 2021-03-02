// SingleConsumer.h

// This is a fast single consumer, multiple producer queue for pointers.

#include <atomic>
#include <cstdint>
#include "futex.h"

inline void cpu_relax() {
// TODO use <boost/fiber/detail/cpu_relax.hpp> when available (>1.65.0?)
#if defined(__i386) || defined(_M_IX86) || defined(__x86_64__) || \
    defined(_M_X64)
#if defined _WIN32
  YieldProcessor();
#else
  asm volatile("pause" ::: "memory");
#endif
#else
  static constexpr std::chrono::microseconds us0{0};
  std::this_thread::sleep_for(us0);
#endif
}

template<typename T, std::size_t capacitylog2>
class alignas(64) LockFreeQueue {
  static constexpr std::size_t const Capacity = 1ul << capacitylog2;
  static constexpr std::size_t const CapMask = Capacity - 1;
  static constexpr std::size_t const StepPrime = 11;  // 11*8 > 64 bytes
  static constexpr std::size_t const Limit = StepPrime * Capacity * 7 / 8;

  // Keep things in different cache lines:
  alignas(64) std::atomic<T*>* _ring;
  alignas(64) std::size_t _head;   // we have _head <= _tail at all times
              std::size_t _headCount;
  alignas(64) std::atomic<std::size_t> _headPub;
  alignas(64) std::atomic<std::size_t> _tail;   // _head == _tail is empty
  alignas(64) Futex _sleeping;
              uint64_t _nrSleeps;

 public:

  LockFreeQueue()
    : _head(0), _headCount(0), _headPub(0), _tail(0), _sleeping(0), _nrSleeps(0) {
    _ring = new std::atomic<T*>[Capacity];
    for (std::size_t i = 0; i < Capacity; ++i) {
      _ring[i].store(nullptr, std::memory_order_relaxed);
    }
  };

  ~LockFreeQueue() {
    for (std::size_t i = 0; i < Capacity; ++i) {
      delete _ring[i].e;
    }
    delete[] _ring;
  }

  uint64_t nrSleeps() const {
    return _nrSleeps;
  }

  // The following methods can be called from multiple threads:
 
  bool try_push(T* p) {
    // First check that there is some space in the queue:
    std::size_t tail = _tail.load(std::memory_order_relaxed);
    std::size_t head = _headPub.load(std::memory_order_relaxed);
    if (tail - head > Limit) {
      return false;
    }
    tail = _tail.fetch_add(StepPrime, std::memory_order_relaxed);
    std::size_t pos = tail & CapMask;
    _ring[pos].store(p, std::memory_order_release);
      // (1) This synchronizes with (2) in try_pop.
    return true;
  }

  bool try_push_with_wakeup(T* p) {
    if (!try_push(p)) {
      return false;
    }
    wakeup();
    return true;
  }

  // The following methods may only be called by a single thread!
 
  bool try_pop(T*& result) {
    std::size_t pos = _head & CapMask;
    T* res = _ring[pos].load(std::memory_order_acquire);
      // (2) This synchronizes with (1) in try_push.
    if (res == nullptr) {
      return false;
    }
    _head += StepPrime;
    if (++_headCount == 1024) {
      _headCount = 0;
      _headPub.store(_head, std::memory_order_relaxed);
    }
    _ring[pos].store(nullptr, std::memory_order_relaxed);
    result = res;
    return true;
  }

  static constexpr int const SpinLimit = 10000;

  void pop_or_sleep(T*& result) {
    while (true) {
      for (int i = 0; i < SpinLimit; ++i) {
        if (try_pop(result)) {
          return;
        }
        cpu_relax();
      }

      ++_nrSleeps;
      // Now try to go to sleep:
      _sleeping.value().store(1, std::memory_order_seq_cst);
      if (try_pop(result)) {
        _sleeping.value().store(0, std::memory_order_relaxed);
        return;
      }
      _sleeping.wait(1);
      _sleeping.value().store(0, std::memory_order_seq_cst);
      // Proof that there is no sleeping barber between pop_or_sleep
      // and try_push_with_wakeup:
      // We only have to proof that the consumer cannot sleep despite
      // the fact that there is something on the queue.
      // If the consumer has gone to sleep, then the futex value was
      // one went it dozed off. That is, the read of _sleeping in
      // wakeup must have happened after that. But then wakeup calls
      // notifyOne and we are good.
    }
  }

  bool empty() const {
    std::size_t pos = _head & CapMask;
    T* res = _ring[pos].load(std::memory_order_acquire);
    return res == nullptr;
  }

 private:
  void wakeup() {
    // To be called by a different thread than the consumer
    if (_sleeping.value().load(std::memory_order_seq_cst) == 1) {
      _sleeping.value().store(0, std::memory_order_seq_cst);
      _sleeping.notifyOne();
    }
  }

};
