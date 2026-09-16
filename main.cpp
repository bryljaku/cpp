#include "queue.h"
#include "smart_pointer.h"
#include "stack.h"
#include <print>
#include <thread>

auto data_structure = SimpleMutexQueue<int>();

void producer() {
  int i = 20;
  while (i > 0) {
    std::println("Producing");
    data_structure.push(i);
    i--;
    sleep(1);
  }
}

void consumer() {
  int capacityLeft = 10;
  while (capacityLeft > 0) {
    if (!data_structure.empty()) {
      std::println("Consuming {}", std::this_thread::get_id());
      data_structure.wait_and_pop();
      capacityLeft--;
    }
  }
}
int main() {
  auto producerThread = std::thread(producer);
  std::vector<std::thread> consumerThreads;
  for (int i = 0; i < 2; ++i) {
    consumerThreads.push_back(std::thread(consumer));
  }
  producerThread.join();
  for (auto &consumerThread : consumerThreads) {
    consumerThread.join();
  }
  return 0;
}
