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
  static_assert(maxNrProducers * 8 < Capacity,
      "Capacity must at least be 8 times the maximal number of consumers!");
      // 3/4 of the Capacity must be less than
  static constexpr std::size_t const CapMask = Capacity - 1;
  static constexpr std::size_t const StepNumber = 9;  // 9*8 > 64 bytes, and
      // 9 is coprime to powers of two
  static constexpr std::size_t const LowWater = Capacity / 4;
  static constexpr std::size_t const HighWater = (Capacity * 3) / 4;
  static constexpr std::size_t const CriticalWater = Capacity - maxNrProducers;

  // Keep things in different cache lines:

  // This is the cache line for everybody to read only:
  alignas(64)
    std::atomic<T*>* _ring;   // the actual ring buffer

  // This is the cache line for the one consumer to work with:
  alignas(64)
    std::size_t _output;   // we have _output <= _input at all times and
                           // values are meant mod Capacity to map to the
                           // ring buffer
    std::size_t _outputCount;  // used to count when to publish _output
    uint64_t _nrSleeps;    // only for statistics

  // This is the cache line for exchange between producer and consumers,
  // mostly read by consumers, occasionally written by the producer:
  alignas(64) 
    std::atomic<std::size_t> _inputLowWater;
    std::atomic<std::size_t> _inputHighWater;
    std::atomic<std::size_t> _inputCriticalWater;
    std::atomic<bool> _filling;   // if true, we are filling up the queue
                                  // until high water is reached, indicates
                                  // fast push path
    Futex _sleeping;     // futex to go to sleep

  // This is the cache line for the producers:
  alignas(64)
    std::atomic<std::size_t> _input;   // _output == _input is empty

 public:

  LockFreeQueue()
    : _output(0), _outputCount(0), _nrSleeps(0), _inputLowWater(LowWater),
      _inputHighWater(HighWater), _inputCriticalWater(CriticalWater),
      _filling(true), _sleeping(0), _input(0) {
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
    std::size_t input;
    std::size_t pos;
    // Distinguish fast and slow path, for non-high water we go fast,
    // in which case _filling is true:
    if (!_filling.load(std::memory_order_relaxed)) {
      // First check that there is some space in the queue:
      input = _input.load(std::memory_order_relaxed);
      if (input >= _inputCriticalWater.load(std::memory_order_relaxed)) {
        return false;   // queue is considered to be full
        // Note that we the _inputCriticalWater mark is maxNrProducers less
        // than Capacity, so we normally leave maxNrProducers slots unused.
        // However, every producer might see _filling to be true and then
        // increase _input by 1, so we could potentially overrun the
        // _inputCriticalWater mark by at most maxNrProducers, so we are good.
      }
      input = _input.fetch_add(1, std::memory_order_relaxed);
      pos = (input * StepNumber) & CapMask;
      _ring[pos].store(p, std::memory_order_release);
        // (1) This synchronizes with (2) in try_pop.
      if (input < _inputLowWater.load(std::memory_order_relaxed)) {
        _filling.store(true, std::memory_order_relaxed);
      }
    } else {
      input = _input.fetch_add(1, std::memory_order_relaxed);
      pos = (input * StepNumber) & CapMask;
      _ring[pos].store(p, std::memory_order_release);
        // (1) This synchronizes with (2) in try_pop.
      if (input >= _inputHighWater.load(std::memory_order_relaxed)) {
        _filling.store(false, std::memory_order_relaxed);
      }
    }
    wakeup();
    return true;
  }

  // The following methods may only be called by a single thread!
 
  bool try_pop(T*& result) {
    std::size_t pos = (_output * StepNumber) & CapMask;
    T* res = _ring[pos].load(std::memory_order_acquire);
      // (2) This synchronizes with (1) in try_push.
    if (res == nullptr) {
      return false;
    }
    _ring[pos].store(nullptr, std::memory_order_relaxed);
    result = res;
    ++_output;
    // Sometimes publish _output increase for limits:
    if (++_outputCount == 1024) {
      _outputCount = 0;
      std::size_t shift = 0;
      if (_output > 256 * Capacity) {
        // Avoid overflow, limits will be
        auto newOutput = _output & CapMask;
        shift = _output - newOutput;
        _output = newOutput;
      }
      _inputCriticalWater.store(_output + CriticalWater, std::memory_order_relaxed);
      _inputHighWater.store(_output + HighWater, std::memory_order_relaxed);
      _inputLowWater.store(_output + LowWater, std::memory_order_relaxed);
      if (shift != 0) {
        _input.fetch_sub(shift, std::memory_order_relaxed);
      }
    }
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
      // and try_push:
      // We only have to proof that the consumer cannot sleep despite
      // the fact that there is something on the queue.
      // If the consumer has gone to sleep, then the futex value was
      // one went it dozed off. That is, the read of _sleeping in
      // wakeup must have happened after that. But then wakeup calls
      // notifyOne and we are good.
    }
  }

  bool empty() const {
    std::size_t pos = _output & CapMask;
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
