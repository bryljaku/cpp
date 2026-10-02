#include "queue.h"
#include "smart_pointer.h"
#include "stack.h"
#include <print>
#include <thread>

FastQueue<int, 1024, 4, true, 8> data_structure{};

void producer(int id) {
  int i = 10;
  while (i > 0) {
    int y = i;
    std::println("Producing {} by thread {}", y, id);
    data_structure.push(&y, id);
    i--;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
}

void consumer(int id) {
  int capacityLeft = 10;
  while (capacityLeft > 0) {
    int res;
    if (data_structure.pop(&res, id)) {
      std::println("Consumed {} by thread {}", res, id);
      capacityLeft--;
    }
  }
}

constexpr int NUM_ITEMS = 5'000'000;
template <typename Q>
double run_benchmark(int num_producers, int num_consumers) {
  Q queue;
  std::atomic_flag start_flag = false;
  std::atomic<int> items_consumed;

  auto producer = [&](uint32_t tid, int items_to_push) {
    start_flag.wait(false, std::memory_order::acquire);
    for (int i = 0; i < items_to_push; ++i) {
      queue.push(tid * items_to_push + i, tid);
    }
  };
  auto consumer = [&](uint32_t tid, int total_items_to_consume) {
    start_flag.wait(false, std::memory_order::acquire);
    while (items_consumed.load(std::memory_order::acquire) <
           total_items_to_consume) {

      queue.pop(tid);
    }
  };
}

int main() { return 0; }
