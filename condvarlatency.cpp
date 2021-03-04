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

void producer(uint64_t nr) {
  while (go.load(std::memory_order_relaxed) == false) {
    cpu_relax();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  std::vector<uint64_t> times;
  times.reserve(nr);
  for (uint64_t i = 0; i < nr; ++i) {
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
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
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
