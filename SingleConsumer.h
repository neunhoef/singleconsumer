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

template<typename T, std::size_t capacitylog2, std::size_t maxNrProducers>
class alignas(64) LockFreeQueue {
  static constexpr std::size_t const Capacity = 1ul << capacitylog2;
  static_assert(capacitylog2 <= 28,
      "Capacity must be at most 2^28!");
  static_assert(maxNrProducers * 4 < Capacity,
      "Capacity must be at least 4 times the maximal number of consumers!");
      // ==> 1/4 of the Capacity must be more than maxNrProducers,
      //     thus Capacity - HighWater > maxNrProducers
  static constexpr std::size_t const CapMask = Capacity - 1;
  static constexpr std::size_t const StepNumber = 9;  // 9*8 > 64 bytes, and
      // 9 is coprime to powers of two
  static constexpr uint32_t const HighWater 
    = static_cast<uint32_t>(((Capacity * 3) / 4) << 1);

  // Keep things in different cache lines:

  // This is the cache line for everybody to read only:
  alignas(64)
    std::atomic<T*>* _ring;   // the actual ring buffer

  // This is the cache line for the one consumer to work with:
  alignas(64)
    uint32_t _output; // _output is the index where the consumer pops from
                      // the queue shifted 1 to the left since we need the
                      // low bit of _input as a sleeping flag and need to
                      // have the same wraparound behaviour in _input and
                      // _output. We have _input - _output <= CriticalWater
                      // at all times. 
    uint32_t _outputCount;  // used to count when to publish _output
    uint64_t _nrSleeps;    // only for statistics

  // This is the cache line for exchange between producer and consumers,
  // mostly read by consumers, occasionally written by the producer:
  // This should be in cache of all producers nearly always:
  alignas(64) 
    std::atomic<uint32_t> _outputPublished;

  // This is the cache line for the producers:
  alignas(64)
    Futex _input; // _input is always stored shifted left one bit,
                  // low bit is sleeping flag, _output == _input means
                  // that the queue is empty

 public:

  LockFreeQueue()
    : _output(0), _outputCount(0), _nrSleeps(0), _outputPublished(0),
      _input(0) {
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
    uint32_t input = static_cast<uint32_t>(
        _input.value().load(std::memory_order_relaxed));
    if (input - _outputPublished.load(std::memory_order_relaxed) >=
        HighWater) {
      // queue is considered to be full, for the case that somebody
      // is retrying constantly, we do a cpu_relax loop for him here:
      // Note that this uses the fact that int is 2s-complement and
      // overflow
      for (size_t i = 0; i < 100; ++i) {
        cpu_relax();
      }
      return false;   
    }
    // Now do the actual push:
    input = static_cast<uint32_t>(
        _input.value().fetch_add(2, std::memory_order_relaxed));
    std::size_t pos = 
      (static_cast<std::size_t>(input >> 1) * StepNumber) & CapMask;
    _ring[pos].store(p, std::memory_order_release);
      // (1) This synchronizes with (2) in try_pop.
    if ((input & 1) != 0) {
      // The consumer is sleeping, so we need to wake it up:
      resetSleepingBit();
      _input.notifyOne();
    }
    return true;
  }

  // The following methods may only be called by a single thread!
 
  bool try_pop(T*& result) {
    std::size_t pos
      = (static_cast<std::size_t>(_output >> 1) * StepNumber) & CapMask;
    T* res = _ring[pos].load(std::memory_order_acquire);
      // (2) This synchronizes with (1) in try_push.
    if (res == nullptr) {
      return false;
    }
    _ring[pos].store(nullptr, std::memory_order_relaxed);
    result = res;
    _output += 2;
    // Sometimes publish _output increase for limits:
    if (++_outputCount == 256) {
      _outputCount = 0;
      _outputPublished.store(_output, std::memory_order_relaxed);
    }
    return true;
  }

  static constexpr int const SpinLimit = 1000;

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
      // Set sleeping bit in _input:
      int input = _input.value().fetch_add(1, std::memory_order_relaxed);
      if (static_cast<uint32_t>(input) == _output) {
        _input.wait(input+1);
      }
      resetSleepingBit();
      // Proof that there is no sleeping barber between pop_or_sleep
      // and try_push:
      // Assume there is a sleeping barber, that is, the consumer sleeps,
      // so it has done _input.wait with success. When this was executed,
      // _input was what was stored to the local variable input and we
      // have checked that this is equal to _output. If any producer has
      // pushed something to the queue to fetch_add _input, then this must
      // have happened later in the modification order of _input and thus
      // have seen the low bit set. Then this producer would have modified
      // _input again by resetting the sleeping it and then called notifyOne.
      // If that notifyOne would have happened before we sleep, then the
      // wait could not have gone through, since the producer would have
      // modified _input beforehand and made it even again.
    }
  }

  bool empty() const {
    std::size_t pos = _output & CapMask;
    T* res = _ring[pos].load(std::memory_order_acquire);
    return res == nullptr;
  }

 private:
  void resetSleepingBit() {
    int input = _input.value().load(std::memory_order_relaxed);
    while ((input & 1) != 0 &&
           !_input.value().compare_exchange_weak(input, input-1,
               std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
  }
};
