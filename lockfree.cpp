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

// Define exactly one of the following three:

//#define SINGLECONSUMER 1
//#define ATOMICQUEUE 1
#define BOOSTLOCKFREE 1

#include "SingleConsumer.h"

#ifdef SINGLECONSUMER
typedef LockFreeQueue<uint64_t, 20> Queue;
#endif

#ifdef ATOMICQUEUE
#include <atomic_queue/atomic_queue.h>
typedef atomic_queue::AtomicQueue<uint64_t*, 1048576, nullptr, true, true, false, false> Queue;
#endif

#ifdef BOOSTLOCKFREE
#include <boost/lockfree/queue.hpp>
typedef boost::lockfree::queue<uint64_t*> Queue;
#endif

std::atomic<bool> go{false};
std::chrono::steady_clock::time_point startTime;
std::chrono::steady_clock::time_point endTime;

void producer(Queue* queue, uint64_t nr) {
  while (go.load(std::memory_order_relaxed) == false) {
    cpu_relax();
  }
  uint64_t* val = new uint64_t[nr];
  for (uint64_t i = 0; i < nr; ++i) {
#ifdef ATOMICQUEUE
    while (!queue->try_push(val)) {
      cpu_relax();
    }
#endif
#ifdef SINGLECONSUMER
    while (!queue->try_push_with_wakeup(val)) {
      cpu_relax();
    }
#endif
#ifdef BOOSTLOCKFREE
    while (!queue->push(val)) {
      cpu_relax();
    }
#endif
    ++val;
  }
}

void consumer(Queue* queue, uint64_t nr) {
  while (go.load(std::memory_order_relaxed) == false) {
    cpu_relax();
  }
  std::this_thread::sleep_for(std::chrono::microseconds(100));
  startTime = std::chrono::steady_clock::now();
  uint64_t* val = nullptr;
  uint64_t counter = 0;
  for (uint64_t i = 0; i < nr; ++i) {
#ifdef ATOMICQUEUE
    for (;;) {
      bool gotit = queue->try_pop(val);
      if (gotit) {
        break;
      }
      ++counter;
      cpu_relax();
    }
#endif
#ifdef SINGLECONSUMER
    queue->pop_or_sleep(val);
#endif
#ifdef BOOSTLOCKFREE
    for (;;) {
      bool gotit = queue->pop(val);
      if (gotit) {
        break;
      }
      ++counter;
      cpu_relax();
    }
#endif
  }
  endTime = std::chrono::steady_clock::now();
  std::cout << "Number of times we saw nothing on the queue: " << counter
    << std::endl;
}

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

#ifdef BOOSTLOCKFREE
  Queue* q = new Queue(1048576);
#else
  Queue* q = new Queue();
#endif
  std::thread cons{&consumer, q, nrThreads * nrOps};
  std::vector<std::thread*> prod;
  for (std::size_t i = 0; i < nrThreads; ++i) {
    prod.push_back(new std::thread(&producer, q, nrOps));
  }

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
#ifdef SINGLECONSUMER
  std::cout << "Number of sleeps: " << q->nrSleeps() << std::endl;
#endif
  return 0;
}
