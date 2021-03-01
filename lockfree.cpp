#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <thread>
#include <vector>

//#define MEINE 1

#ifndef MEINE
#include <atomic_queue/atomic_queue.h>
#endif

template<typename T, std::size_t capacitylog2>
class LockFreeQueue {
  static constexpr std::size_t const Capacity = 1ul << capacitylog2;
  static constexpr std::size_t const CapMask = Capacity - 1;
  static constexpr std::size_t const StepPrime = 11;  // 11*8 > 64 bytes
  static constexpr std::size_t const Limit = StepPrime * Capacity * 7 / 8;

  struct Entry {
    std::atomic<T*> e;
    char padding[56];
  };

  std::atomic<T*>* _ring;
  char padding[56];
  std::size_t _head;   // we have _head <= _tail at all times
  std::size_t _headCount;
  char padding2[48];
  std::atomic<std::size_t> _headPub;
  char padding3[56];
  std::atomic<std::size_t> _tail;   // _head == _tail is empty

 public:

  LockFreeQueue() : _head(0), _headCount(0), _headPub(0), _tail(0)  {
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

  bool try_push(T* p) {
    // First check that there is some space in the queue:
    std::size_t tail = _tail.load(std::memory_order_relaxed);
    std::size_t head = _headPub.load(std::memory_order_relaxed);
    if (tail - head > Limit) {
      return false;
    }
    tail = _tail.fetch_add(StepPrime, std::memory_order_relaxed);
    std::size_t pos = tail & CapMask;
    _ring[pos].store(p, std::memory_order_relaxed);
    return true;
  }

  bool try_pop(T*& result) {
    std::size_t pos = _head & CapMask;
    T* res = _ring[pos].load(std::memory_order_acquire);
    if (res == nullptr) {
      return false;
    }
    _head += StepPrime;
    if (++_headCount == 256) {
      _headCount = 0;
      _headPub.store(_head, std::memory_order_relaxed);
    }
    _ring[pos].store(nullptr, std::memory_order_relaxed);
    result = res;
    return true;
  }

  bool empty() const {
    std::size_t pos = _head & CapMask;
    T* res = _ring[pos].load(std::memory_order_acquire);
    return res == nullptr;
  }
};

#ifdef MEINE
typedef LockFreeQueue<uint64_t, 20> Queue;
#else
typedef atomic_queue::AtomicQueue<uint64_t*, 1024000, nullptr, true, true, false, false> Queue;
#endif

std::atomic<bool> go{false};
std::atomic<std::size_t> running{0};
std::chrono::steady_clock::time_point endTime;

void producer(Queue* queue, uint64_t nr) {
  while (go.load(std::memory_order_relaxed) == false) {
    std::this_thread::yield();
  }
  uint64_t* val = new uint64_t[nr];
  for (uint64_t i = 0; i < nr; ++i) {
    while (!queue->try_push(val)) {
      //std::this_thread::yield();
    }
    ++val;
  }
  std::size_t done = running.fetch_sub(1, std::memory_order_relaxed);
  if (done == 1) {
    endTime = std::chrono::steady_clock::now();
  }
}

void consumer(Queue* queue, uint64_t nr) {
  while (go.load(std::memory_order_relaxed) == false) {
    std::this_thread::yield();
  }
  uint64_t* val = nullptr;
  uint64_t counter = 0;
  for (uint64_t i = 0; i < nr; ++i) {
    for (;;) {
      bool gotit = queue->try_pop(val);
      if (gotit) {
        break;
      }
      ++counter;
      //std::this_thread::yield();
    }
  }
  std::size_t done = running.fetch_sub(1, std::memory_order_relaxed);
  if (done == 1) {
    endTime = std::chrono::steady_clock::now();
  }
  std::cout << "Number of times we saw nothing on the queue: " << counter
    << std::endl;
}

#if 0
int main(int argc, char* argv[]) {
  LockFreeQueue<uint64_t, 8> q;
  uint64_t* x = new uint64_t(17);
  uint64_t* y = new uint64_t(18);
  q.push(x);
  q.push(y);
  assert(!q.empty());
  uint64_t* z = q.pop();
  assert(z == x);
  assert(!q.empty());
  z = q.pop();
  assert(z == y);
  z = q.pop();
  assert(z == nullptr);
  assert(q.empty());
  delete y;
  delete x;
  return 0;
}
#endif

int main(int argc, char* argv[]) {
  std::size_t nrThreads = 1;
  std::size_t nrOps = 10000000;
  if (argc > 1) {
    nrThreads = std::strtoul(argv[1], nullptr, 10);
  }
  if (argc > 2) {
    nrOps = std::strtoul(argv[2], nullptr, 10);
  }
  std::cout << "nrThreads=" << nrThreads << std::endl;

  Queue* q = new Queue();
  std::thread cons{&consumer, q, nrThreads * nrOps};
  std::vector<std::thread*> prod;
  for (std::size_t i = 0; i < nrThreads; ++i) {
    prod.push_back(new std::thread(&producer, q, nrOps));
  }

  running = nrThreads + 1;
  auto startTime = std::chrono::steady_clock::now();
  go = true;
  for (std::size_t i = 0; i < nrThreads; ++i) {
    prod[i]->join();
  }
  cons.join();

  uint64_t nanoseconds
    = std::chrono::nanoseconds(endTime - startTime).count();
  std::cout << "Total time: " <<  nanoseconds << " ns for "
    << nrThreads * nrOps << " items, which is " << (double) nanoseconds / (nrOps * nrThreads)
    << " ns/item" << std::endl;
  return 0;
}
