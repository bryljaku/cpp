#include "queue.h"
#include "smart_pointer.h"
#include "stack.h"
#include <print>
#include <thread>

constexpr int NUM_ITEMS = 50;
template <typename Q>
double run_benchmark(int num_producers, int num_consumers) {
  Q queue;
  std::atomic_flag start_flag = false;
  std::atomic<int> items_consumed;

  auto producer = [&](int tid, int items_to_push) {
    std::println("{} waiting on flag", tid);
    start_flag.wait(false, std::memory_order::acquire);
    std::println("{} finished waiting on flag", tid);
    for (int i = 0; i < items_to_push; ++i) {
      int to_push = tid * items_to_push + i;
      queue.push(&to_push, tid);
      std::println("{} produced {}", tid, i);
    }
  };

  auto consumer = [&](int tid, int total_items_to_consume) {
    std::println("{} waiting on flag", tid);
    start_flag.wait(false, std::memory_order::acquire);
    std::println("{} finished waiting on flag", tid);
    while (items_consumed.load(std::memory_order::relaxed) <
           total_items_to_consume) {
      int val;
      if (queue.pop(&val, tid)) {
        std::println("{} consumed total {}", tid,
                     items_consumed.load(std::memory_order::relaxed));
        items_consumed.fetch_add(1, std::memory_order::relaxed);
      }
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(num_consumers + num_producers);

  for (int i = 0; i < num_producers; ++i) {
    threads.emplace_back(producer, i, NUM_ITEMS / std::max(1, num_producers));
  }
  for (int i = 0; i < num_consumers; ++i) {
    threads.emplace_back(consumer, num_producers + i, NUM_ITEMS);
  }

  auto start_time = std::chrono::high_resolution_clock::now();
  start_flag.test_and_set();
  start_flag.notify_all();
  for (auto &thread : threads) {
    thread.join();
  }
  auto end_time = std::chrono::high_resolution_clock::now();

  return std::chrono::duration<double, std::milli>(end_time - start_time)
      .count();
}

template <typename Q>
void run_scenario(const std::string &label, int num_producers,
                  int num_consumers) {
  std::println("Starting benchmark!");
  std::println("{}({}, {}): {}ms", label, num_producers, num_consumers,
               run_benchmark<Q>(num_producers, num_consumers));
}

int main() {
  using Fast = FastQueue<int, 1024, 8, false, 256>;
  run_scenario<Fast>("Fast", 1, 2);
  return 0;
}
