// condvarlatency.cpp

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <thread>
#include <vector>

#include "SingleConsumer.h"

struct TwoTimes {
  std::chrono::steady_clock::time_point start;
  std::chrono::steady_clock::time_point end;
  std::atomic<bool> done;
};

typedef LockFreeQueue<TwoTimes, 20, 64> Queue;

std::atomic<bool> go{false};
std::mutex mutex;
std::condition_variable condvar;
TwoTimes tt;

uint64_t total = 0;

void busywait(uint64_t ns) {
  auto start = std::chrono::steady_clock::now();
  uint64_t l = 10000;
  while (true) {
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::nanoseconds(now - start).count() >= ns) {
      return;
    }
    uint64_t x = 0;
    for (uint64_t i = 0; i < l; ++i) {
      x += i * i * i;
    }
    total += x;
    l = l + 1700;
    if (l > 20000) {
      l -= 10000;
    }
  }
}

void producer(uint64_t nr) {
  while (go.load(std::memory_order_relaxed) == false) {
    cpu_relax();
  }
  std::vector<uint64_t> times;
  times.reserve(nr);
  uint64_t l = 100000000;
  for (uint64_t i = 0; i < nr; ++i) {
    busywait(l);
    l += 123456;
    if (l > 150000000) {
      l -= 50000000;
    }
    {
      std::lock_guard<std::mutex> guard(mutex);
      tt.start = std::chrono::steady_clock::now();
      tt.done = false;
      condvar.notify_one();
    }
    while (!tt.done) {
      cpu_relax();
    }
    tt.end = std::chrono::steady_clock::now();
    times.push_back(std::chrono::nanoseconds(tt.end - tt.start).count());
  }
  std::sort(times.begin(), times.end());
  {
    std::lock_guard<std::mutex> guard(mutex);
    std::cout << "Latencies: median=" << times[nr / 2]
              << " 90%ile=" << times[nr * 9 / 10]
              << " 99%ile=" << times[nr * 99 / 100]
              << " smallest=" << times[0]
              << std::endl;
    std::cout << "largest 10:";
    for (uint64_t i = nr - 10; i < nr; ++i) {
      std::cout << " " << times[i];
    }
    std::cout << std::endl;
  }
}

void consumer(uint64_t nr) {
  while (go.load(std::memory_order_relaxed) == false) {
    cpu_relax();
  }
  for (uint64_t i = 0; i < nr; ++i) {
    std::unique_lock<std::mutex> guard(mutex);
    condvar.wait(guard);
    tt.done = true;
  }
}

int main(int argc, char* argv[]) {
  std::size_t nrThreads = 1;
  std::size_t nrOps = 200;
  if (argc > 1) {
    nrThreads = std::strtoul(argv[1], nullptr, 10);
  }
  if (argc > 2) {
    nrOps = std::strtoul(argv[2], nullptr, 10);
  }
  std::cout << "nrThreads=" << nrThreads << std::endl;

  std::thread cons{&consumer, nrThreads * nrOps};
  std::vector<std::thread*> prod;
  for (std::size_t i = 0; i < nrThreads; ++i) {
    prod.push_back(new std::thread(&producer, nrOps));
  }

  go = true;
  for (std::size_t i = 0; i < nrThreads; ++i) {
    prod[i]->join();
  }
  cons.join();
  return 0;
}
